# gst-plugins-android

CPU-only software audio decoders for GStreamer, built on **Android 16's Codec2 (C2) software
components** lifted directly from AOSP and ported to glibc Linux.

This subproject is a **reference solution** that lets embedded Linux platforms drop their
per-SoC OMX/DSP integrations in favour of a single, deterministic, fully open SW pipeline.

It ships six GStreamer audio decoder elements:

| Element        | Wraps                       | Sink caps                                  | External decoder            | Verified vs reference           |
| -------------- | --------------------------- | ------------------------------------------ | --------------------------- | ------------------------------- |
| `c2opusdec`    | `c2.android.opus.decoder`   | `audio/x-opus`                             | system libopus              | bit-exact (libopus)             |
| `c2aacdec`     | `c2.android.aac.decoder`    | `audio/mpeg, mpegversion={2,4}`            | system libfdk-aac           | corr 1.0 vs ffmpeg              |
| `c2flacdec`    | `c2.android.flac.decoder`   | `audio/x-flac, framed=true`                | system libFLAC + wrapper    | bit-exact (lossless)            |
| `c2vorbisdec`  | `c2.android.vorbis.decoder` | `audio/x-vorbis`                           | vendored Tremolo (fixed-pt) | corr 1.0 / RMS 0.7 vs libvorbis |
| `c2mp3dec`     | `c2.android.mp3.decoder`    | `audio/mpeg, mpegversion=1, layer=[1,3]`   | vendored pvmp3 (PacketVideo)| corr 1.0 / RMS 0.5 vs ffmpeg    |
| `c2iamfdec` †  | — (AOSP component is a stub)| `audio/x-iamf`                             | vendored AOM libiamf v1.0.1 | bit-exact (44/44 vectors)       |

All output `audio/x-raw, format=S16LE, layout=interleaved`. None require the AOSP
libFraunhoferAAC fork or any BY_EXCEPTION_ONLY-licensed code: AAC uses upstream
mstorsjo fdk-aac; FLAC uses the distro libFLAC; Vorbis/MP3 vendor the Apache/BSD
Tremolo and pvmp3 decoder sources from AOSP.

> † **IAMF is the exception to the "wrap a C2 component" rule.** AOSP *names* a
> `c2.android.iamf.decoder`, but in `android-16.0.0_r1` it is an empty stub
> (`Android.bp srcs:[]`, `process()` is a no-op). So `c2iamfdec` is a plain
> `GstAudioDecoder` over AOM's reference decoder **libiamf** (BSD-3-Clause-Clear),
> not the C2 bridge. IAMF descriptors are in-band and there is no GStreamer IAMF
> parser, so the element accepts the raw OBU bytestream (`audio/x-iamf`, e.g.
> straight from `filesrc`) and feeds libiamf's incremental consume-as-you-go API
> via a `GstAdapter`. A typefinder for `audio/x-iamf` (OBU type 31 + `iamf`
> magic) lets `decodebin`/`playbin` autoplug it. Output defaults to stereo
> 48 kHz; the `sound-system` property selects 5.1 (`1`), 7.1 (`8`), mono (`12`),
> etc., and `binaural=true` does a headphone downmix. Verified **bit-identical**
> to the upstream `iamfdec` CLI across 44/44 sampled conformance vectors, in both
> stereo and 5.1.

## Architecture

```
   GstPipeline                                                user space, Linux/glibc
   ──────────────────────────────────────────────────────────────────────────────────
   ┌──────────────────┐   GstAudioDecoder vfuncs   ┌───────────────────────────────┐
   │ c2opusdec /      │ ◀────── handle_frame ────▶ │ gstc2common.cc                │
   │ c2aacdec         │                            │   SimpleC2Component owner     │
   │ (C, plugin .so)  │ ◀───── finish_frame ─────  │   sync queue+wait on Listener │
   └──────────────────┘                            └───────────────┬───────────────┘
                                                                   ▼
                                                ┌──────────────────────────────────┐
                                                │ libc2components.a                │
                                                │   C2SoftOpusDec / C2SoftAacDec   │
                                                │   SimpleC2Component (ALooper)    │
                                                └──────────────────┬───────────────┘
                                                                   ▼
                                                ┌──────────────────────────────────┐
                                                │ libc2vndk.a + libc2sf.a          │
                                                │   AMessage/ALooper/AHandler      │
                                                │   C2Buffer (linear only)         │
                                                │   C2InterfaceHelper              │
                                                └──────────────────┬───────────────┘
                                                                   ▼
                                                ┌──────────────────────────────────┐
                                                │ libc2porting.a                   │
                                                │   liblog / libutils / cutils     │
                                                │   android-base shims             │
                                                │   C2LinuxMallocAllocator         │
                                                │   C2Store_linux (singleton)      │
                                                └──────────────────────────────────┘
                                                                   ▼
                                                ┌──────────────────────────────────┐
                                                │ system libopus + libfdk-aac      │
                                                │ (or wrap subprojects)            │
                                                └──────────────────────────────────┘
```

No HIDL, no AIDL, no binder, no gralloc, no `/vendor/etc/media_codecs.xml`. The audio
decoders never touch graphic buffers.

## Source provenance

AOSP files are **not committed** to this tree. They are fetched at configure time from
[android.googlesource.com](https://android.googlesource.com/platform/frameworks/av/) at
tag `android-16.0.0_r1`. Run:

```sh
python3 scripts/fetch_aosp_sources.py --out aosp
```

(meson does this automatically the first time you configure if `aosp/` is empty.)

The list of fetched files is encoded in `scripts/fetch_aosp_sources.py` itself, mirroring
the design's `file_download_list`. Each file is dropped under `aosp/...` keeping its
original AOSP relative path so its `#include` lines work unmodified.

**libiamf** (for `c2iamfdec`) is fetched separately by `scripts/fetch_libiamf.py`
(a shallow clone of the `v1.0.1` release tag of
[github.com/AOMediaCodec/libiamf](https://github.com/AOMediaCodec/libiamf)) into the
gitignored `iamf/upstream/`. The tag is pinned: `main`/HEAD pulls a *private*
submodule (`oar-private`, binaural renderer) and needs cmake ≥ 3.28, whereas `v1.0.1`
is self-contained and builds with our meson glue (`iamf/meson.build`) against the same
system opus/fdk-aac/FLAC. This is gated by `-Dwith_iamf` (default `auto`); set
`-Dwith_iamf=disabled` to skip IAMF entirely.

## Build

```sh
# from gst-plugins-android/
meson setup build
ninja -C build
GST_PLUGIN_PATH=build/src gst-inspect-1.0 androidcodec2

# smoke pipelines (one per codec)
GST_PLUGIN_PATH=build/src gst-launch-1.0 filesrc location=clip.opus ! oggdemux       ! c2opusdec   ! audioconvert ! audioresample ! autoaudiosink
GST_PLUGIN_PATH=build/src gst-launch-1.0 filesrc location=clip.aac  ! aacparse       ! c2aacdec    ! audioconvert ! autoaudiosink
GST_PLUGIN_PATH=build/src gst-launch-1.0 filesrc location=clip.flac ! flacparse      ! c2flacdec   ! audioconvert ! autoaudiosink
GST_PLUGIN_PATH=build/src gst-launch-1.0 filesrc location=clip.ogg  ! oggdemux       ! c2vorbisdec ! audioconvert ! autoaudiosink
GST_PLUGIN_PATH=build/src gst-launch-1.0 filesrc location=clip.mp3  ! mpegaudioparse ! c2mp3dec    ! audioconvert ! autoaudiosink
# IAMF: raw .iamf needs no parser (descriptors are in-band); decodebin works via the typefinder.
GST_PLUGIN_PATH=build/src gst-launch-1.0 filesrc location=clip.iamf !                   c2iamfdec  ! audioconvert ! audioresample ! autoaudiosink
GST_PLUGIN_PATH=build/src gst-launch-1.0 filesrc location=clip.iamf ! c2iamfdec sound-system=1 ! audioconvert ! autoaudiosink   # 5.1 render
```

To use the elements without `GST_PLUGIN_PATH` on every invocation, copy the built
plugin into the user plugin dir (no sudo):
`cp build/src/gstandroidcodec2.so ~/.local/share/gstreamer-1.0/plugins/`.

Dependencies:

- meson ≥ 1.1, ninja
- gcc ≥ 11 (the vendored AOSP layer is compiled with `-std=gnu++17`)
- glib-2.0, gstreamer-1.0 ≥ 1.20 (`gstreamer-base-1.0`, `gstreamer-audio-1.0`, `gstreamer-pbutils-1.0`)
- libopus ≥ 1.4 (`pkg-config opus`)
- libfdk-aac ≥ 2.0 (`pkg-config fdk-aac`), or fall back via `with_system_fdk_aac=false`
- libFLAC (`pkg-config flac`) — for `c2flacdec` and for IAMF's FLAC substreams
- Python 3 + git (only at configure time, for the fetchers)
- For `c2iamfdec`: opus + fdk-aac + FLAC (all already required above); libiamf v1.0.1
  itself is vendored/built, not a system package. Disable with `-Dwith_iamf=disabled`.

Vorbis (`c2vorbisdec`) and MP3 (`c2mp3dec`) need no external dev package: the
fixed-point Tremolo and PacketVideo pvmp3 decoder sources are vendored from AOSP
(external/tremolo and frameworks/av media/module/codecs/mp3dec) by the fetcher.
The distro `libvorbisidec` is a different, incompatible Tremor fork and is NOT used.

## Runtime configuration

| Env var                        | Meaning                                                                       |
| ------------------------------ | ----------------------------------------------------------------------------- |
| `GST_C2_LOG_LEVEL`             | `off` / `error` / `warning` / `info` / `verbose` (default `warning`)          |
| `GST_C2_AAC_51_OUTPUT_ENABLED` | If set, force 5.1 downmix-off behaviour (maps to AOSP's `media.aac_51_output_enabled`) |

## Element ranks

The five C2 elements register at `GST_RANK_PRIMARY - 1`, so the existing `opusdec` /
`avdec_aac` / `fdkaacdec` / `flacparse+avdec_flac` / `vorbisdec` / `avdec_mp3` continue to
win `decodebin` autoplug unless explicitly preferred via an explicit pipeline
(`... ! c2opusdec ! ...`) or a custom `decoder-rank-*` policy.

`c2iamfdec` registers at `GST_RANK_PRIMARY` (with its `audio/x-iamf` typefinder), since no
other IAMF decoder ships in a stock GStreamer — there is nothing to defer to.

## Licensing

- This subproject's glue code (`src/*.c`, `src/*.cc`, `porting/*.cpp`) is
  **LGPL-2.1-or-later** (matching gstreamer).
- The vendored AOSP files keep their original Apache-2.0 headers; they are statically
  linked into `gstandroidcodec2.so`. LGPL-2.1 permits static linking with code under
  permissive licences. See `LICENSE.Apache-2.0` and `LICENSE.LGPL-2.1`.
- FDK-AAC: when statically linked through the `fdk-aac.wrap` fallback, the Fraunhofer
  notice in `COPYING.fdk-aac.notice` must be reproduced in distribution.

## Status

Working reference: all six elements build, register, and decode real streams, each
verified against an independent reference decoder (see the table above). Opus and FLAC
are bit-exact; AAC/Vorbis/MP3 use a different decoder implementation than their reference
(FDK / Tremolo / pvmp3 vs ffmpeg/libvorbis) so they match by correlation rather than
bit-for-bit, which is expected. IAMF (`c2iamfdec`) shares libiamf with its `iamfdec`
reference, so it is **bit-exact** (44/44 sampled conformance vectors, stereo and 5.1).
The AOSP sources are fetched at configure time and patched only via the reproducible
`scripts/apply_local_patches.py` (no in-tree AOSP edits); libiamf is fetched at the pinned
`v1.0.1` tag and built unmodified by `iamf/meson.build`.

Possible follow-ups: an optional platform audio-interface layer (resource acquisition / sink
routing / low-latency hooks behind a build option); additional SW codecs (raw/g711/amr);
a neutral-DRC option for AAC; IAMF MP4/ISOBMFF demuxing (`.mp4` carriage; today the element
takes the raw `.iamf` OBU stream). See the design notes for the full list.
