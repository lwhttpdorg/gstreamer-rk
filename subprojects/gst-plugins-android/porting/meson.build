# ─── porting/ ──────────────────────────────────────────────────────────────
# Linux shims for Android system headers + the C2 platform-support entry
# points (allocator + component store) the rest of the C2 stack expects.
#
# The single libc2porting_dep declared at the bottom is what the other layers
# (aosp/ and src/) depend on.
# ───────────────────────────────────────────────────────────────────────────

porting_inc = include_directories(
  'include',                              # all the <log/log.h>, <utils/*>, <android-base/*>, <cutils/*>, <android/*>, <system/*>, <hardware/*>, <media/stagefright/MediaErrors.h>
  '../aosp/core/include',                 # C2.h, C2Buffer.h, C2Component.h, ...
  '../aosp/vndk/include',                 # C2Debug.h, C2PlatformSupport.h, C2BufferPriv.h, C2ComponentFactory.h, ...
  '../aosp/vndk/include/util',            # C2InterfaceHelper.h, C2InterfaceUtils.h, ...
  '../aosp/vndk/internal',                # C2BlockInternal.h, C2ParamInternal.h
  '../aosp/libstagefright/foundation/include',
  # AOSP .cpp files use bare quotes/angle-brackets to include sibling headers
  # (e.g. AMessage.cpp says #include "AMessage.h"). Add the leaf directory so
  # those resolve identically to AOSP's own build (which exports
  # local_include_dirs=["include/media/stagefright/foundation"]).
  '../aosp/libstagefright/foundation/include/media/stagefright/foundation',
)

porting_cpp_args = [
  '-DGST_C2_LINUX_PORT',
  '-D__unused=__attribute__((__unused__))',   # see aosp/meson.build for why
  '-D__LITTLE_ENDIAN__=1',
  '-fno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wno-deprecated-declarations',
  '-Wno-multichar',
  '-Wno-non-virtual-dtor',
  # Force-include the std-header prelude so porting TUs that include C2 headers
  # (C2LinuxMallocAllocator.cpp, C2Store_linux.cpp) get cstddef/limits/cstring
  # before C2Param.h/C2Buffer.h use std::numeric_limits/offsetof/memcmp.
  # gcc's -include wants the filename as a separate token (glued form is a no-op).
  '-include', 'log/log.h',
]

# Map the meson 'logging' option to a compile-time default.
logging_to_macro = {
  'off':     'ANDROID_LOG_SILENT',
  'error':   'ANDROID_LOG_ERROR',
  'warning': 'ANDROID_LOG_WARN',
  'info':    'ANDROID_LOG_INFO',
  'verbose': 'ANDROID_LOG_VERBOSE',
}
porting_cpp_args += [
  '-DGST_C2_DEFAULT_LOG_LEVEL=@0@'.format(
    logging_to_macro[get_option('logging')]),
]

libc2porting = static_library('c2porting',
  [
    'gst_c2_log.cpp',
    'native_handle_stub.cpp',
    'properties_stub.cpp',
    'hardware_buffer_stub.cpp',
    'utils_string8.cpp',
    'libyuv_stub.cpp',
    'AStringUtils_stub.cpp',
    'C2LinuxMallocAllocator.cpp',
    'C2Store_linux.cpp',
  ],
  include_directories : porting_inc,
  cpp_args : porting_cpp_args,
  dependencies : [threads_dep],
  pic : true,
  install : false,
)

libc2porting_dep = declare_dependency(
  link_with : libc2porting,
  include_directories : porting_inc,
  compile_args : ['-DGST_C2_LINUX_PORT'],
  dependencies : [threads_dep],
)
