/*
 * gst-plugins-android — gstc2common: GstAudioDecoder <-> SimpleC2Component bridge.
 *
 * Each instance owns:
 *   - one C2Component (Opus or AAC) created via GetCodec2PlatformComponentStore()
 *   - a Listener that signals a condition variable when a Work item is done
 *   - a private mutex protecting the in-flight Work pending map
 *
 * Concurrency: gst_c2_component_decode() is called from GStreamer's streaming
 * thread. It builds one C2Work, queues it on the component, then blocks on a
 * std::condition_variable until the Listener fires. We never overlap calls
 * from the streaming thread; if upstream parsing produces multiple frames the
 * decoder is driven serially, frame-by-frame.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "gstc2common.h"
#include "C2Store_linux.h"
#include "C2LinuxMallocAllocator.h"

#include <C2.h>
#include <C2Buffer.h>
#include <C2BufferPriv.h>
#include <C2Component.h>
#include <C2Config.h>
#include <C2PlatformSupport.h>
#include <C2Work.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define LOG_TAG "gstc2common"
#include <log/log.h>

GST_DEBUG_CATEGORY_STATIC (gst_c2_common_debug);
#define GST_CAT_DEFAULT gst_c2_common_debug

/* Lightweight stderr tracer, enabled with GST_C2_TRACE=1, for pinpointing
 * where the C2 lifecycle blocks (the ALooper message pump is the prime
 * suspect for any hang). */
static inline bool gst_c2_trace_on(void) {
  const char* v = getenv("GST_C2_TRACE");
  return v && v[0] && !(v[0] == '0' && v[1] == '\0');  /* unset/""/"0" => off */
}
#define GST_C2_TRACE(...) do { \
    if (gst_c2_trace_on()) { \
      fprintf(stderr, "[C2TRACE] " __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } \
  } while (0)

namespace {

const char* codec_name(GstC2Codec c) {
  switch (c) {
    case GST_C2_CODEC_OPUS:   return "c2.android.opus.decoder";
    case GST_C2_CODEC_AAC:    return "c2.android.aac.decoder";
    case GST_C2_CODEC_FLAC:   return "c2.android.flac.decoder";
    case GST_C2_CODEC_VORBIS: return "c2.android.vorbis.decoder";
    case GST_C2_CODEC_MP3:    return "c2.android.mp3.decoder";
  }
  return nullptr;
}

/* -------------------------------------------------------------------------
 * Listener — wakes the streaming thread once the queued Work comes back.
 * ------------------------------------------------------------------------- */
struct WorkSlot {
  uint64_t                      frame_index;
  std::unique_ptr<C2Work>       result;
  bool                          ready{false};
};

class WaitListener : public C2Component::Listener {
 public:
  void onWorkDone_nb(std::weak_ptr<C2Component> /*component*/,
                      std::list<std::unique_ptr<C2Work>> workItems) override {
    std::unique_lock<std::mutex> lk(mMutex);
    for (auto& w : workItems) {
      const uint64_t idx = w->input.ordinal.frameIndex.peekull();
      GST_C2_TRACE("onWorkDone fi=%llu result=%d outBufs=%zu",
                   (unsigned long long)idx, w->result,
                   w->worklets.empty() ? 0 : w->worklets.front()->output.buffers.size());
      mResults[idx] = std::move(w);
      ++mTotalDone;
    }
    mCv.notify_all();
  }

  void onTripped_nb(std::weak_ptr<C2Component> /*component*/,
                     std::vector<std::shared_ptr<C2SettingResult>> /*settingResult*/) override {
    ALOGW("component tripped");
  }

  void onError_nb(std::weak_ptr<C2Component> /*component*/, uint32_t errorCode) override {
    ALOGE("component error: 0x%x", errorCode);
    std::lock_guard<std::mutex> lk(mMutex);
    mError = errorCode;
    mCv.notify_all();
  }

  /* Block until at least `min_total` works have completed in total (across the
   * whole session) or the timeout elapses. Returns false on timeout/error.
   * Used to let the component's async looper make progress: a decoder with
   * pipeline delay D returns work N-D when work N is queued, so callers wait
   * for "one more completion than last time" rather than for a specific
   * frameIndex (which would deadlock against the delay). */
  bool wait_until_total(uint64_t min_total, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mMutex);
    return mCv.wait_for(lk, timeout, [&]{ return mError != 0 || mTotalDone >= min_total; });
  }

  /* Remove and return every completed work currently held, in ascending
   * frameIndex order (C2 guarantees in-order completion for these SW decoders,
   * but the map keeps us robust regardless). */
  std::vector<std::unique_ptr<C2Work>> take_ready() {
    std::vector<std::unique_ptr<C2Work>> out;
    std::lock_guard<std::mutex> lk(mMutex);
    for (auto& kv : mResults) out.push_back(std::move(kv.second));
    mResults.clear();
    return out;
  }

  uint64_t total_done() {
    std::lock_guard<std::mutex> lk(mMutex);
    return mTotalDone;
  }

  uint32_t error() const { return mError; }
  void     reset_error() {
    std::lock_guard<std::mutex> lk(mMutex);
    mError = 0; mResults.clear();
  }

 private:
  std::mutex                                   mMutex;
  std::condition_variable                      mCv;
  std::map<uint64_t, std::unique_ptr<C2Work>>  mResults;
  uint64_t                                     mTotalDone = 0;
  uint32_t                                     mError = 0;
};

}  /* namespace */

/* -------------------------------------------------------------------------
 * Opaque struct surfaced to C.
 * ------------------------------------------------------------------------- */
/* One decoded PCM buffer waiting to be handed to GstAudioDecoder. */
struct PcmOut {
  uint8_t* data;   /* g_malloc'd, transfer-full to the caller */
  size_t   size;
  int      rate;
  int      channels;
};

struct GstC2Component {
  GstC2Codec                          codec;
  std::shared_ptr<C2Component>        component;
  std::shared_ptr<WaitListener>       listener;
  std::shared_ptr<C2BlockPool>        input_pool;     /* linear pool for input C2Buffer */

  std::atomic<uint64_t>               next_frame_index{0};
  uint64_t                            works_queued = 0;   /* total queued (incl. CSD) */
  bool                                started = false;

  /* Outputs completed by the component but not yet pulled by the element.
   * Decoders with pipeline delay (AAC) return work N-D when work N is queued,
   * so output lags input; this FIFO absorbs that lag. */
  std::deque<PcmOut>                  out_fifo;

  /* Cached after first decoded frame. */
  int                                  out_rate     = 0;
  int                                  out_channels = 0;

  std::string                          name;
};

/* -------------------------------------------------------------------------
 * Helpers.
 * ------------------------------------------------------------------------- */
namespace {

bool ensure_input_pool(GstC2Component* self) {
  if (self->input_pool) return true;
  auto alloc = gst_c2::GetLinuxLinearAllocator();
  /* C2BasicLinearBlockPool is part of C2BufferPriv.h — a plain malloc-backed
   * pool that simply forwards to the allocator on each request. */
  self->input_pool = std::make_shared<C2BasicLinearBlockPool>(alloc);
  return self->input_pool != nullptr;
}

std::unique_ptr<C2Work> build_work(GstC2Component* self,
                                    const uint8_t* in, size_t in_size,
                                    uint64_t pts_ns,
                                    bool eos) {
  auto work = std::make_unique<C2Work>();
  const uint64_t fi = self->next_frame_index.fetch_add(1, std::memory_order_relaxed);

  work->input.flags = (eos ? C2FrameData::FLAG_END_OF_STREAM : (C2FrameData::flags_t)0);
  work->input.ordinal.frameIndex   = fi;
  work->input.ordinal.timestamp    = pts_ns / 1000;  /* C2 ordinal uses us */
  work->input.ordinal.customOrdinal = pts_ns / 1000;
  work->worklets.emplace_back(std::make_unique<C2Worklet>());

  if (in && in_size > 0) {
    std::shared_ptr<C2LinearBlock> block;
    c2_status_t s = self->input_pool->fetchLinearBlock(
        (uint32_t)in_size,
        { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE },
        &block);
    if (s != C2_OK || !block) {
      ALOGE("fetchLinearBlock failed: %d", s);
      return nullptr;
    }
    {
      C2WriteView view = block->map().get();
      if (view.error() != C2_OK) {
        ALOGE("LinearBlock map failed: %d", view.error());
        return nullptr;
      }
      std::memcpy(view.data(), in, in_size);
    }
    work->input.buffers.push_back(
        C2Buffer::CreateLinearBuffer(block->share(0, in_size, C2Fence())));
  }

  return work;
}

/* Extract the PCM from one completed work into a PcmOut. Returns false (and a
 * zero-size PcmOut) if the work carried no output (CSD/priming frames). */
bool work_to_pcm(C2Work* work, GstC2Component* self, PcmOut* pcm) {
  pcm->data = nullptr; pcm->size = 0; pcm->rate = self->out_rate; pcm->channels = self->out_channels;

  if (!work || work->worklets.empty()) return false;
  auto& wl = work->worklets.front();
  GST_C2_TRACE("work_to_pcm: out.buffers=%zu workResult=%d", wl->output.buffers.size(), work->result);
  if (wl->output.buffers.empty()) return false;

  std::shared_ptr<C2Buffer> outbuf = wl->output.buffers.front();
  if (!outbuf || outbuf->data().linearBlocks().empty()) return false;

  C2ConstLinearBlock blk = outbuf->data().linearBlocks().front();
  C2ReadView v = blk.map().get();
  if (v.error() != C2_OK) {
    ALOGE("output LinearBlock map failed: %d", v.error());
    return false;
  }
  pcm->size = v.capacity();
  pcm->data = (uint8_t*)g_malloc(pcm->size);
  std::memcpy(pcm->data, v.data(), pcm->size);

  /* Pull rate/channels from the component interface (cache once). */
  if (self->out_rate == 0 || self->out_channels == 0) {
    C2StreamSampleRateInfo::output  sr(0u);
    C2StreamChannelCountInfo::output cc(0u);
    std::vector<C2Param*> stack{&sr, &cc};
    self->component->intf()->query_vb(stack, {}, C2_DONT_BLOCK, nullptr);
    self->out_rate     = (int)sr.value;
    self->out_channels = (int)cc.value;
  }
  pcm->rate     = self->out_rate;
  pcm->channels = self->out_channels;
  return true;
}

/* Move every currently-completed work from the listener into the output FIFO,
 * in frameIndex order. Non-output works (CSD/priming) are dropped. */
void collect_ready(GstC2Component* self) {
  auto works = self->listener->take_ready();
  for (auto& w : works) {
    PcmOut pcm;
    if (work_to_pcm(w.get(), self, &pcm) && pcm.size > 0) {
      self->out_fifo.push_back(pcm);
    }
  }
}

}  /* namespace */

/* -------------------------------------------------------------------------
 * Public C API.
 * ------------------------------------------------------------------------- */
extern "C" {

void _gst_c2_common_init_debug(void) {
  GST_DEBUG_CATEGORY_INIT (gst_c2_common_debug, "gstc2common", 0,
      "GStreamer <-> Codec2 SW audio bridge");
}

GstC2Component* gst_c2_component_new(GstC2Codec codec) {
  _gst_c2_common_init_debug();
  GST_C2_TRACE("component_new codec=%d", codec);
  const char* n = codec_name(codec);
  if (!n) return nullptr;

  auto* self = new (std::nothrow) GstC2Component;
  if (!self) return nullptr;
  self->codec = codec;
  self->name  = n;

  c2_status_t s = gst_c2::CreateLinuxC2Component(n, &self->component);
  if (s != C2_OK || !self->component) {
    ALOGE("create '%s' failed: %d", n, s);
    delete self;
    return nullptr;
  }

  GST_C2_TRACE("created component name=%s ok", n);
  self->listener = std::make_shared<WaitListener>();
  s = self->component->setListener_vb(self->listener, C2_MAY_BLOCK);
  if (s != C2_OK) {
    ALOGE("setListener_vb failed: %d", s);
    delete self;
    return nullptr;
  }
  GST_C2_TRACE("listener set, pool ensured");
  ensure_input_pool(self);
  return self;
}

void gst_c2_component_free(GstC2Component* self) {
  if (!self) return;
  if (self->started) {
    self->component->stop();
    self->component->release();
    self->started = false;
  }
  delete self;
}

gboolean gst_c2_component_configure(GstC2Component* self,
                                     gint sample_rate, gint channels,
                                     const guint8* codec_data, gsize codec_data_size) {
  if (!self) return FALSE;

  /* Both the Opus and AAC IntfImpl expose sample-rate / channel-count ONLY as
   * the ::output specialization (keys raw.sample-rate / raw.channel-count).
   * Configuring ::input returns C2_BAD_INDEX (a silent no-op). */
  std::vector<std::unique_ptr<C2Param>> params;
  params.emplace_back(std::make_unique<C2StreamSampleRateInfo::output>(
      0u, (uint32_t)(sample_rate > 0 ? sample_rate : 48000)));
  params.emplace_back(std::make_unique<C2StreamChannelCountInfo::output>(
      0u, (uint32_t)(channels > 0 ? channels : 2)));

  /* AAC framing. C2SoftAacDec's AAC_PACKAGING_ADTS path strips the ADTS header
   * and feeds the bare payload to FDK — which only works with AOSP's
   * libFraunhoferAAC fork. With upstream fdk-aac we instead open the decoder
   * with TT_MP4_ADTS (see apply_local_patches.py) and feed the FULL ADTS frame,
   * which requires the strip path to stay OFF, i.e. packaging left at the
   * RAW default. So we deliberately do NOT set AAC_PACKAGING_ADTS here. */
  (void) codec_data_size;

  std::vector<C2Param*> raw;
  for (auto& p : params) raw.push_back(p.get());

  std::vector<std::unique_ptr<C2SettingResult>> failures;
  c2_status_t s = self->component->intf()->config_vb(raw, C2_MAY_BLOCK, &failures);
  if (s != C2_OK) {
    ALOGW("config_vb sample_rate/channels returned %d (%zu failures)", s, failures.size());
    /* Not fatal — many SoftDecs ignore these on input and derive from CSD. */
  }
  if (self->codec == GST_C2_CODEC_AAC && getenv("GST_C2_TRACE")) {
    C2StreamAacFormatInfo::input fmt(0u);
    std::vector<C2Param*> q{&fmt};
    self->component->intf()->query_vb(q, {}, C2_DONT_BLOCK, nullptr);
    GST_C2_TRACE("aac packaging read-back = %u (ADTS=%d RAW=%d)", (unsigned)fmt.value,
                 (int)C2Config::AAC_PACKAGING_ADTS, (int)C2Config::AAC_PACKAGING_RAW);
  }
  /* Codec-specific data (Opus header / AAC ASC). We pass it as the very first
   * Work item, marked CODEC_CONFIG. */
  GST_C2_TRACE("configure rate=%d ch=%d csd=%zu", sample_rate, channels, (size_t)codec_data_size);
  if (codec_data && codec_data_size > 0) {
    auto work = build_work(self, codec_data, (size_t)codec_data_size, 0, false);
    if (!work) return FALSE;
    work->input.flags = (C2FrameData::flags_t)(work->input.flags | C2FrameData::FLAG_CODEC_CONFIG);
    std::list<std::unique_ptr<C2Work>> items;
    items.push_back(std::move(work));
    self->works_queued++;
    c2_status_t qs = self->component->queue_nb(&items);
    if (qs != C2_OK) {
      ALOGE("queue CSD failed: %d", qs);
      return FALSE;
    }
    /* Let the CSD work complete so the decoder is configured before frames. */
    self->listener->wait_until_total(self->works_queued, std::chrono::milliseconds(200));
    collect_ready(self);   /* CSD produces no PCM, but clears the result map */
  }
  return TRUE;
}

gboolean gst_c2_component_queue_csd(GstC2Component* self,
                                     const guint8* data, gsize size) {
  /* Submit one codec-config buffer as its own FLAG_CODEC_CONFIG work. Used by
   * Vorbis, which needs TWO separate header works (identification + setup) that
   * the component detects by content — unlike the single codec_data blob that
   * gst_c2_component_configure() submits for Opus/AAC. Produces no PCM. */
  if (!self || !data || size == 0) return FALSE;
  auto work = build_work(self, data, (size_t)size, 0, false);
  if (!work) return FALSE;
  work->input.flags = (C2FrameData::flags_t)(work->input.flags | C2FrameData::FLAG_CODEC_CONFIG);
  std::list<std::unique_ptr<C2Work>> items;
  items.push_back(std::move(work));
  self->works_queued++;
  c2_status_t qs = self->component->queue_nb(&items);
  if (qs != C2_OK) {
    ALOGE("queue_csd failed: %d", qs);
    return FALSE;
  }
  self->listener->wait_until_total(self->works_queued, std::chrono::milliseconds(200));
  collect_ready(self);
  GST_C2_TRACE("queue_csd size=%zu done", (size_t)size);
  return TRUE;
}

gboolean gst_c2_component_start(GstC2Component* self) {
  if (!self) return FALSE;
  if (self->started) return TRUE;
  GST_C2_TRACE("calling component->start()");
  c2_status_t s = self->component->start();
  if (s != C2_OK) { ALOGE("start failed: %d", s); return FALSE; }
  GST_C2_TRACE("component->start() returned OK");
  self->started = true;
  return TRUE;
}

gboolean gst_c2_component_stop(GstC2Component* self) {
  if (!self || !self->started) return TRUE;
  c2_status_t s = self->component->stop();
  self->started = false;
  return s == C2_OK;
}

void gst_c2_component_flush(GstC2Component* self) {
  if (!self || !self->started) return;
  std::list<std::unique_ptr<C2Work>> flushed;
  self->component->flush_sm(C2Component::FLUSH_COMPONENT, &flushed);
  self->listener->reset_error();
  /* Drop any buffered outputs and in-flight works (seek/flush). */
  for (auto& p : self->out_fifo) g_free(p.data);
  self->out_fifo.clear();
  self->out_rate = self->out_channels = 0;
}

GstFlowReturn gst_c2_component_decode(GstC2Component* self,
                                       const guint8* in, gsize in_size,
                                       GstClockTime in_pts_ns,
                                       gboolean eos) {
  GST_C2_TRACE("decode in_size=%zu eos=%d", (size_t)in_size, (int)eos);
  if (!self->started) return GST_FLOW_FLUSHING;

  auto work = build_work(self, in, (size_t)in_size,
                          in_pts_ns == GST_CLOCK_TIME_NONE ? 0 : in_pts_ns,
                          eos != FALSE);
  if (!work) return GST_FLOW_ERROR;

  /* Snapshot completions BEFORE queuing: a decoder with pipeline delay D
   * returns work N-D when work N is queued, so we wait for "one more completion
   * than before" rather than for this exact work (which won't complete until D
   * more frames arrive — that would stall every frame for the full timeout).
   * At steady state this returns in ~1 ms; only the first D priming frames
   * (no completion yet) hit the timeout, and the tail is flushed at EOS. */
  const uint64_t prev_done = self->listener->total_done();

  std::list<std::unique_ptr<C2Work>> batch;
  batch.push_back(std::move(work));
  ++self->works_queued;

  c2_status_t qs = self->component->queue_nb(&batch);
  if (qs != C2_OK) {
    ALOGE("queue_nb failed: %d", qs);
    return GST_FLOW_ERROR;
  }

  self->listener->wait_until_total(prev_done + 1, std::chrono::milliseconds(300));
  if (self->listener->error()) {
    ALOGE("decode: listener error 0x%x", self->listener->error());
    return GST_FLOW_ERROR;
  }
  collect_ready(self);
  return GST_FLOW_OK;
}

void gst_c2_component_drain(GstC2Component* self) {
  if (!self || !self->started) return;
  GST_C2_TRACE("drain: flushing %llu queued works", (unsigned long long)self->works_queued);
  /* Ask the component to emit everything it is holding (decoder delay tail). */
  self->component->drain_nb(C2Component::DRAIN_COMPONENT_WITH_EOS);
  /* Give the looper a moment to push out the remaining works. */
  self->listener->wait_until_total(self->works_queued, std::chrono::milliseconds(500));
  collect_ready(self);
}

gboolean gst_c2_component_pull(GstC2Component* self,
                               guint8** out_pcm, gsize* out_size,
                               gint* out_rate, gint* out_channels) {
  *out_pcm = nullptr; *out_size = 0;
  *out_rate = self->out_rate; *out_channels = self->out_channels;
  if (self->out_fifo.empty()) return FALSE;
  PcmOut p = self->out_fifo.front();
  self->out_fifo.pop_front();
  *out_pcm = p.data; *out_size = p.size;
  *out_rate = p.rate; *out_channels = p.channels;
  GST_C2_TRACE("pull pcm=%zu rate=%d ch=%d (fifo left=%zu)", p.size, p.rate, p.channels,
               self->out_fifo.size());
  return TRUE;
}

const gchar* gst_c2_component_get_name(GstC2Component* self) {
  return self ? self->name.c_str() : "(null)";
}

}  /* extern "C" */
