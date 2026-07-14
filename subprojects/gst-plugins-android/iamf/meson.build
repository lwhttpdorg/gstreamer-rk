# ─── iamf/ — AOM libiamf (the IAMF reference decoder), built into the plugin ─
#
# IAMF (Immersive Audio Model & Formats) is the odd one out: AOSP *has* a
# Codec2 component name `c2.android.iamf.decoder`, but in android-16.0.0_r1 it
# is an empty STUB (Android.bp srcs:[], process() is a no-op). So unlike the
# other five codecs, IAMF cannot follow the "wrap an AOSP C2 component" model.
# Instead we vendor AOM's reference decoder, libiamf (BSD-3-Clause-Clear), and
# drive it directly from a standalone GstAudioDecoder (src/gstc2iamfdec.c).
#
# The upstream tree is fetched (gitignored) into iamf/upstream/ by
# scripts/fetch_libiamf.py at configure time. We replicate libiamf's own
# CMake recipe in meson: 24 .c sources + the wav writer, linked against the
# same system opus / fdk-aac / FLAC we already use for the C2 codecs. Binaural
# rendering (BEAR/Resonance) stays OFF — those paths are #ifdef-guarded and
# need external libs we don't ship.
# ───────────────────────────────────────────────────────────────────────────

iamf_code = 'upstream' / 'code'

libiamf_srcs = files(
  # common
  iamf_code / 'src/common/fixedp11_5.c',
  # decoder core
  iamf_code / 'src/iamf_dec/audio_effect_peak_limiter.c',
  iamf_code / 'src/iamf_dec/bitstream.c',
  iamf_code / 'src/iamf_dec/demixer.c',
  iamf_code / 'src/iamf_dec/downmix_renderer.c',
  iamf_code / 'src/iamf_dec/h2b_rdr.c',          # HOA->binaural: stub when binaural off
  iamf_code / 'src/iamf_dec/h2m_rdr.c',
  iamf_code / 'src/iamf_dec/IAMF_core_decoder.c',
  iamf_code / 'src/iamf_dec/IAMF_debug_sr.c',
  iamf_code / 'src/iamf_dec/IAMF_decoder.c',
  iamf_code / 'src/iamf_dec/IAMF_OBU.c',
  iamf_code / 'src/iamf_dec/IAMF_utils.c',
  iamf_code / 'src/iamf_dec/m2b_rdr.c',          # multichannel->binaural: stub when off
  iamf_code / 'src/iamf_dec/m2m_rdr.c',
  iamf_code / 'src/iamf_dec/queue_t.c',
  iamf_code / 'src/iamf_dec/resample.c',
  iamf_code / 'src/iamf_dec/vlogging_tool_sr.c',
  # per-codec substream wrappers (system libs)
  iamf_code / 'src/iamf_dec/opus/IAMF_opus_decoder.c',
  iamf_code / 'src/iamf_dec/opus/opus_multistream2_decoder.c',
  iamf_code / 'src/iamf_dec/aac/aac_multistream_decoder.c',
  iamf_code / 'src/iamf_dec/aac/IAMF_aac_decoder.c',
  iamf_code / 'src/iamf_dec/flac/flac_multistream_decoder.c',
  iamf_code / 'src/iamf_dec/flac/IAMF_flac_decoder.c',
  iamf_code / 'src/iamf_dec/pcm/IAMF_pcm_decoder.c',
  # wav writer (referenced by IAMF_debug_sr.c)
  iamf_code / 'dep_external/src/wav/dep_wavwriter.c',
)

# Internal include layout. /usr/include is a gcc default, so the wrappers'
# "opus/opus.h" / "fdk-aac/aacdecoder_lib.h" / "FLAC/stream_decoder.h" resolve
# without an explicit -I (the codec deps below add the link libs).
libiamf_inc = include_directories(
  iamf_code / 'include',
  iamf_code / 'src/common',
  iamf_code / 'src/iamf_dec',
  iamf_code / 'src/iamf_dec/opus',
  iamf_code / 'src/iamf_dec/aac',
  iamf_code / 'src/iamf_dec/flac',
  iamf_code / 'dep_external/include',
)

# Defines mirror libiamf's CMakeLists (CODEC_CAP on => all three codec paths).
# We do NOT define HAVE_CONFIG_H, so the `#include "config.h"` lines stay
# disabled (there is no generated config.h at this tag).
libiamf_c_args = cc.get_supported_arguments([
  '-DIAMF_PRIMARY_PROFILE=001',
  '-DIAMF_ADDITIONAL_PROFILE=001',
  '-DCONFIG_OPUS_CODEC',
  '-DCONFIG_AAC_CODEC',
  '-DCONFIG_FLAC_CODEC',
  '-DFLAC__NO_DLL',
  '-fno-strict-aliasing',
  '-Wno-unused-variable',
  '-Wno-unused-function',
  '-Wno-unused-but-set-variable',
  '-Wno-unused-result',
  '-Wno-sign-compare',
  '-Wno-pointer-sign',
  '-Wno-implicit-fallthrough',
  '-Wno-discarded-qualifiers',
  '-Wno-maybe-uninitialized',
])

libiamf = static_library('iamf',
  libiamf_srcs,
  include_directories : libiamf_inc,
  c_args : libiamf_c_args,
  dependencies : [opus_dep, fdk_aac_dep, flac_dep],
  install : false,
  pic : true,
)

# Consumers (the c2iamfdec element) only need the public header dir.
libiamf_dep = declare_dependency(
  link_with : libiamf,
  include_directories : include_directories(iamf_code / 'include'),
  dependencies : [opus_dep, fdk_aac_dep, flac_dep],
)

have_iamf = true
message('IAMF: building libiamf from ' + (meson.current_source_dir() / iamf_code))
