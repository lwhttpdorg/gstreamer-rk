# ─── aosp/ — AOSP Codec2 core + vndk + components + libstagefright ─────────
#
# This meson.build compiles the vendored AOSP sources into a single static
# library (libc2) that the plugin layer (src/) links against. The layout
# mirrors AOSP's component hierarchy:
#   core/      — C2 public headers + C2.cpp
#   vndk/      — C2Buffer, C2Config, interface/param utilities
#   components/— per-codec SW implementations (opus, aac, flac, vorbis, mp3)
#   libstagefright/foundation/ — AMessage/AString/ABuffer/OpusHeader helpers
#   codecs/    — lower-level codec wrappers (FLACDecoder, pvmp3)
#   tremolo/   — fixed-point Vorbis decoder (from external/tremolo)
# ───────────────────────────────────────────────────────────────────────────

aosp_cpp_args = [
  '-DGST_C2_LINUX_PORT',
  '-D__unused=__attribute__((__unused__))',
  '-D__ANDROID_API__=10000',
  '-D__LITTLE_ENDIAN__=1',
  '-std=gnu++17',
  '-fms-extensions',
  '-fpermissive',
  '-fno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wno-deprecated-declarations',
  '-Wno-non-virtual-dtor',
  '-Wno-multichar',
  '-Wno-mismatched-tags',
  '-Wno-attributes',
  '-Wno-implicit-fallthrough',
  '-Wno-missing-field-initializers',
  # Force-include the log prelude and Timers so the C2 headers resolve their
  # systemTime() / ALOG* macros identically to how they do in the porting layer.
  '-include', 'log/log.h',
  '-include', 'utils/Timers.h',
  # Force-include ADebug.h (CHECK macro) and system headers (HAL_PIXEL_FORMAT,
  # ANDROID_PRIORITY) used by SimpleC2Component.cpp but not always transitively
  # included by the AOSP header graph under gcc/linux.
  '-include', 'media/stagefright/foundation/ADebug.h',
  '-include', 'system/graphics.h',
  '-include', 'system/thread_defs.h',
  # OSCL macro used by pvmp3 (PacketVideo) — trivial void cast
  '-DOSCL_UNUSED_ARG(x)=(void)(x)',
]

aosp_c_args = [
  '-DGST_C2_LINUX_PORT',
  '-D__unused=__attribute__((__unused__))',
  '-D__ANDROID_API__=10000',
  '-D__LITTLE_ENDIAN__=1',
  '-fno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wno-sign-compare',
  '-Wno-implicit-fallthrough',
  '-Wno-pointer-sign',
  '-Wno-maybe-uninitialized',
  # Tremolo's C files need stdint.h for INT32_MAX/MIN
  '-include', 'stdint.h',
  # Force pure-C Tremolo path (no ARM32 assembly) on aarch64
  '-DONLY_C',
]

# Include paths — reuse porting_inc from the porting layer, plus local dirs
aosp_inc = include_directories(
  'core/include',
  'vndk/include',
  'vndk/include/util',
  'vndk/internal',
  'components/base/include',
  'components/aac',
  'components/flac',
  'components/opus',
  'components/vorbis',
  'components/mp3',
  'libstagefright/foundation/include',
  'libstagefright/foundation/include/media/stagefright/foundation',
  'codecs/flac/dec',
  'codecs/mp3dec/include',
  'codecs/mp3dec/src',
  'tremolo',
)

# FLAC dependency (system libFLAC) — used by C2SoftFlacDec + FLACDecoder
flac_dep = dependency('flac', required : true)

# AOSP sources do #include <aacdecoder_lib.h> but system fdk-aac installs
# headers under a fdk-aac/ subdirectory. A shim header in porting/include/
# redirects to <fdk-aac/aacdecoder_lib.h>, so no -I manipulation needed.

# ─── Sources ──────────────────────────────────────────────────────────────

aosp_cpp_sources = files(
  # C2 core
  'core/C2.cpp',
  # C2 vndk
  'vndk/C2Buffer.cpp',
  'vndk/C2Config.cpp',
  'vndk/util/C2Debug.cpp',
  'vndk/util/C2InterfaceHelper.cpp',
  'vndk/util/C2InterfaceUtils.cpp',
  'vndk/util/C2ParamUtils.cpp',
  # libstagefright foundation
  'libstagefright/foundation/ABuffer.cpp',
  'libstagefright/foundation/ADebug.cpp',
  'libstagefright/foundation/AHandler.cpp',
  'libstagefright/foundation/ALooper.cpp',
  'libstagefright/foundation/ALooperRoster.cpp',
  'libstagefright/foundation/AMessage.cpp',
  'libstagefright/foundation/AString.cpp',
  'libstagefright/foundation/hexdump.cpp',
  'libstagefright/foundation/MediaDefs.cpp',
  'libstagefright/foundation/OpusHeader.cpp',
  # SimpleC2Component base
  'components/base/SimpleC2Component.cpp',
  'components/base/SimpleC2Interface.cpp',
  # Per-codec SW components — compiled separately below with factory renames
  # Codec wrappers
  'codecs/flac/dec/FLACDecoder.cpp',
  # pvmp3 (PacketVideo MP3 decoder)
  'codecs/mp3dec/src/pvmp3_normalize.cpp',
  'codecs/mp3dec/src/pvmp3_alias_reduction.cpp',
  'codecs/mp3dec/src/pvmp3_crc.cpp',
  'codecs/mp3dec/src/pvmp3_decode_header.cpp',
  'codecs/mp3dec/src/pvmp3_decode_huff_cw.cpp',
  'codecs/mp3dec/src/pvmp3_getbits.cpp',
  'codecs/mp3dec/src/pvmp3_dequantize_sample.cpp',
  'codecs/mp3dec/src/pvmp3_framedecoder.cpp',
  'codecs/mp3dec/src/pvmp3_get_main_data_size.cpp',
  'codecs/mp3dec/src/pvmp3_get_side_info.cpp',
  'codecs/mp3dec/src/pvmp3_get_scale_factors.cpp',
  'codecs/mp3dec/src/pvmp3_mpeg2_get_scale_data.cpp',
  'codecs/mp3dec/src/pvmp3_mpeg2_get_scale_factors.cpp',
  'codecs/mp3dec/src/pvmp3_mpeg2_stereo_proc.cpp',
  'codecs/mp3dec/src/pvmp3_huffman_decoding.cpp',
  'codecs/mp3dec/src/pvmp3_huffman_parsing.cpp',
  'codecs/mp3dec/src/pvmp3_tables.cpp',
  'codecs/mp3dec/src/pvmp3_imdct_synth.cpp',
  'codecs/mp3dec/src/pvmp3_mdct_18.cpp',
  'codecs/mp3dec/src/pvmp3_mdct_6.cpp',
  'codecs/mp3dec/src/pvmp3_dct_6.cpp',
  'codecs/mp3dec/src/pvmp3_poly_phase_synthesis.cpp',
  'codecs/mp3dec/src/pvmp3_equalizer.cpp',
  'codecs/mp3dec/src/pvmp3_seek_synch.cpp',
  'codecs/mp3dec/src/pvmp3_stereo_proc.cpp',
  'codecs/mp3dec/src/pvmp3_reorder.cpp',
  'codecs/mp3dec/src/pvmp3_polyphase_filter_window.cpp',
  'codecs/mp3dec/src/pvmp3_dct_9.cpp',
  'codecs/mp3dec/src/pvmp3_dct_16.cpp',
)

# Tremolo (fixed-point Vorbis) — compiled as C
# Note: framing.c is #included by bitwise.c, not compiled separately.
aosp_c_sources = files(
  'tremolo/Tremolo/bitwise.c',
  'tremolo/Tremolo/codebook.c',
  'tremolo/Tremolo/dsp.c',
  'tremolo/Tremolo/floor0.c',
  'tremolo/Tremolo/floor1.c',
  'tremolo/Tremolo/floor_lookup.c',
  'tremolo/Tremolo/mapping0.c',
  'tremolo/Tremolo/mdct.c',
  'tremolo/Tremolo/misc.c',
  'tremolo/Tremolo/res012.c',
  'tremolo/Tremolo/treminfo.c',
  # vorbisfile.c is the full ogg-file-based API — not needed since C2SoftVorbisDec
  # receives raw vorbis packets from GStreamer's ogg demuxer.
)

tremolo_inc = include_directories('tremolo')

# ─── Static library ──────────────────────────────────────────────────────

libc2_cpp = static_library('c2_cpp',
  aosp_cpp_sources,
  include_directories : [porting_inc, aosp_inc],
  cpp_args : aosp_cpp_args,
  dependencies : [libc2porting_dep, opus_dep, fdk_aac_dep, flac_dep, threads_dep],
  pic : true,
  install : false,
)

libc2_c = static_library('c2_tremolo',
  aosp_c_sources,
  include_directories : [porting_inc, tremolo_inc, aosp_inc],
  c_args : aosp_c_args,
  pic : true,
  install : false,
)

# ─── Per-codec components with renamed factory exports ────────────────────
# Each AOSP codec defines CreateCodec2Factory/DestroyCodec2Factory. We compile
# them with -D to rename to unique per-codec names that C2Store_linux.cpp expects.

codec_components = {
  'opus':   {'srcs': ['components/opus/C2SoftOpusDec.cpp'],
             'create': 'CreateOpusDecoderFactory', 'destroy': 'DestroyOpusDecoderFactory'},
  'aac':    {'srcs': ['components/aac/C2SoftAacDec.cpp', 'components/aac/DrcPresModeWrap.cpp'],
             'create': 'CreateAacDecoderFactory', 'destroy': 'DestroyAacDecoderFactory'},
  'flac':   {'srcs': ['components/flac/C2SoftFlacDec.cpp'],
             'create': 'CreateFlacDecoderFactory', 'destroy': 'DestroyFlacDecoderFactory'},
  'vorbis': {'srcs': ['components/vorbis/C2SoftVorbisDec.cpp'],
             'create': 'CreateVorbisDecoderFactory', 'destroy': 'DestroyVorbisDecoderFactory'},
  'mp3':    {'srcs': ['components/mp3/C2SoftMp3Dec.cpp'],
             'create': 'CreateMp3DecoderFactory', 'destroy': 'DestroyMp3DecoderFactory'},
}

codec_libs = []
foreach name, cfg : codec_components
  codec_libs += static_library('c2_@0@'.format(name),
    files(cfg['srcs']),
    include_directories : [porting_inc, aosp_inc],
    cpp_args : aosp_cpp_args + [
      '-DCreateCodec2Factory=@0@'.format(cfg['create']),
      '-DDestroyCodec2Factory=@0@'.format(cfg['destroy']),
    ],
    dependencies : [libc2porting_dep, opus_dep, fdk_aac_dep, flac_dep, threads_dep],
    pic : true,
    install : false,
  )
endforeach

# Aggregated dependency for consumers (src/meson.build, tests/meson.build)
# Link order: tremolo provides oggpack_* used by the vorbis codec, and the
# codec libs use SimpleC2Component from libc2_cpp. Use --whole-archive on the
# static libs to ensure all symbols are available regardless of link order.
libc2_dep = declare_dependency(
  link_whole : [libc2_cpp, libc2_c] + codec_libs,
  include_directories : [porting_inc, aosp_inc],
  dependencies : [libc2porting_dep, opus_dep, fdk_aac_dep, flac_dep, threads_dep],
)
