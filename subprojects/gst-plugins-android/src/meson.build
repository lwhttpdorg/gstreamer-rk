# ─── src/ — the GStreamer plugin shared object ─────────────────────────────

porting_dir_inc = include_directories('../porting')  # C2Store_linux.h / C2LinuxMallocAllocator.h

# gstc2common.cc is the only TU here that #includes the AOSP Codec2 headers, so
# it needs the exact same "C2 consumer" compile environment the aosp/ layer
# uses (otherwise C2Buffer.h's __unused-decorated virtuals mis-parse and
# std::numeric_limits / the block-pool overrides break). We isolate it in its
# own static library so the pure-C plugin/element files don't inherit C++-only
# flags like -std=gnu++17.
c2_consumer_cpp_args = [
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
  '-DPACKAGE="gst-plugins-android"',
  '-DVERSION="@0@"'.format(meson.project_version()),
  # Force-include the std-header prelude (numeric_limits/cstddef/...) and
  # systemTime() so the C2 headers parse exactly as in the aosp/ layer.
  '-include', 'log/log.h',
  '-include', 'utils/Timers.h',
]

libgstc2bridge = static_library('gstc2bridge',
  ['gstc2common.cc'],
  include_directories : [build_inc, porting_dir_inc],
  cpp_args : c2_consumer_cpp_args,
  dependencies : [
    gst_dep, gst_base_dep, gst_audio_dep, gst_pbutils_dep,
    glib_dep, gobject_dep,
    libc2_dep,
  ],
  install : false,
)

plugin_c_args = [
  '-DHAVE_CONFIG_H',
  '-DPACKAGE="gst-plugins-android"',
  '-DGST_PACKAGE_NAME="GStreamer Android Codec2 plugin"',
  '-DGST_PACKAGE_ORIGIN="https://github.com/changyongahn/PoC_GstAnd"',
  '-DVERSION="@0@"'.format(meson.project_version()),
]

plugin_sources = [
  'plugin.c',
  'gstc2opusdec.c',
  'gstc2aacdec.c',
  'gstc2flacdec.c',
  'gstc2vorbisdec.c',
  'gstc2mp3dec.c',
]
plugin_deps = [
  gst_dep, gst_base_dep, gst_audio_dep, gst_pbutils_dep,
  glib_dep, gobject_dep,
  libc2_dep,
]

# IAMF element — built only if libiamf was fetched + configured (see top
# meson.build). It is a plain GstAudioDecoder over libiamf, not a C2 wrapper,
# so it links libiamf_dep directly and is gated behind -DHAVE_IAMF.
if have_iamf
  plugin_sources += 'gstc2iamfdec.c'
  plugin_deps    += libiamf_dep
  plugin_c_args  += '-DHAVE_IAMF'
endif

gstandroidcodec2 = shared_library('gstandroidcodec2',
  plugin_sources,
  include_directories : [build_inc, porting_dir_inc],
  c_args   : plugin_c_args,
  dependencies : plugin_deps,
  link_with : [libgstc2bridge],
  name_prefix : '',
  install : true,
  install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)

# Generate a pkg-config file so downstream gst-build setups can locate the
# plugin's elements via pkg-config gstreamer-android-codec2-1.0.
pkgconfig.generate(
  name : 'gstreamer-android-codec2',
  description : 'GStreamer Android 16 Codec2 SW audio decoders',
  filebase : 'gstreamer-android-codec2-1.0',
  version : meson.project_version(),
  requires : ['gstreamer-1.0', 'gstreamer-audio-1.0'],
  libraries : [gstandroidcodec2],
)
