project('gst-plugins-android', 'c', 'cpp',
  version : '0.1.0',
  meson_version : '>= 1.1',
  default_options : [
    'c_std=gnu11',
    'cpp_std=c++17',
    'warning_level=1',
    'b_ndebug=if-release',
    'buildtype=debugoptimized',
  ])

# ---------------------------------------------------------------------------
# Hard host-system gate. The whole point of this subproject is to run Android's
# Codec2 SW components on a Linux host; building it on Android itself is
# nonsensical (that's what the existing 'androidmedia' plugin in gst-plugins-bad
# already covers, via NDK MediaCodec).
# ---------------------------------------------------------------------------
host_system = host_machine.system()
if host_system != 'linux'
  error('gst-plugins-android only supports linux hosts (got @0@)'.format(host_system))
endif

cc  = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
python3 = import('python').find_installation('python3')
pkgconfig = import('pkgconfig')

# ---------------------------------------------------------------------------
# Core GStreamer / GLib deps. We require GStreamer 1.24 because the design
# uses gst_codec_utils_opus_parse_header / aac caps helpers added in 1.22+.
# ---------------------------------------------------------------------------
# GstAudioDecoder + the opus/aac caps helpers we use exist since 1.16 — we
# only need 1.20 for the C++17-friendly enum tweaks in pbutils. Inside the
# gstreamer monorepo this naturally resolves to whatever the parent ships
# (1.24+ at the time of writing).
gst_req     = '>= 1.20.0'
glib_dep    = dependency('glib-2.0',   version : '>= 2.64')
gobject_dep = dependency('gobject-2.0', version : '>= 2.64')
gst_dep        = dependency('gstreamer-1.0',         version : gst_req)
gst_base_dep   = dependency('gstreamer-base-1.0',    version : gst_req)
gst_audio_dep  = dependency('gstreamer-audio-1.0',   version : gst_req)
gst_pbutils_dep= dependency('gstreamer-pbutils-1.0', version : gst_req)
threads_dep    = dependency('threads')

# Audio codecs we wrap. Prefer system packages; fall back to upstream wraps if
# the host distro lacks them. NB: we deliberately do NOT vendor AOSP's
# external/aac (the Fraunhofer fork) — its BY_EXCEPTION_ONLY licence flag makes
# redistribution risky. The mstorsjo/fdk-aac upstream package has a compatible
# 2.0.x API and is what almost every distro packages as libfdk-aac.
if get_option('with_system_opus')
  opus_dep = dependency('opus', version : '>= 1.4', required : true)
else
  opus_dep = dependency('opus', fallback : ['opus', 'libopus_dep'], required : true)
endif

if get_option('with_system_fdk_aac')
  fdk_aac_dep = dependency('fdk-aac', version : '>= 2.0.0', required : true)
else
  fdk_aac_dep = dependency('fdk-aac', fallback : ['fdk-aac', 'fdk_aac_dep'], required : true)
endif

# ---------------------------------------------------------------------------
# Fetch AOSP sources at configure time. We treat the aosp/ directory as a
# build artefact (it lives in source tree only as a convenience — .gitignored).
# Re-running configure with an already-populated aosp/ is a fast no-op.
# ---------------------------------------------------------------------------
aosp_dir   = meson.current_source_dir() / 'aosp'
aosp_tag   = get_option('aosp_tag')
fetch_opt  = get_option('aosp_fetch')

aosp_marker = aosp_dir / '.fetch.stamp'
if not import('fs').exists(aosp_marker)
  if fetch_opt.disabled()
    error('aosp/ is empty and -Daosp_fetch=disabled. Either populate ' + aosp_dir +
          ' manually or enable -Daosp_fetch=enabled.')
  endif
  message('Fetching AOSP sources at tag ' + aosp_tag + ' into ' + aosp_dir + ' ...')
  fetch_res = run_command(python3,
    files('scripts/fetch_aosp_sources.py'),
    '--out', aosp_dir,
    '--tag', aosp_tag,
    check : false)
  if fetch_res.returncode() != 0
    message('AOSP fetch stdout:\n' + fetch_res.stdout())
    message('AOSP fetch stderr:\n' + fetch_res.stderr())
    if fetch_opt.enabled()
      error('AOSP source fetch failed (see logs above).')
    else
      warning('AOSP fetch did not fully succeed; the build will fail later. ' +
              'Re-run with network access, or supply --tarball-mirror.')
    endif
  endif
endif

# Record what we shipped, for `gst-inspect-1.0` metadata.
ver_cfg = configuration_data()
ver_cfg.set_quoted('GST_C2_AOSP_TAG', aosp_tag)
ver_cfg.set_quoted('GST_C2_PACKAGE_VERSION', meson.project_version())
configure_file(
  input  : 'porting/include/aosp_version.h.in',
  output : 'aosp_version.h',
  configuration : ver_cfg,
)
build_inc = include_directories('.')  # for the generated aosp_version.h

# ---------------------------------------------------------------------------
# Per-layer subdirs. Each one declares an internal dependency that the next
# layer up consumes.
# ---------------------------------------------------------------------------
subdir('porting')   # libc2porting_dep
subdir('aosp')      # libc2_dep (rolls porting + core + sf + vndk + components)

# ---------------------------------------------------------------------------
# IAMF (AOM libiamf) — NOT an AOSP Codec2 component (that one is a stub in
# android-16), so it is fetched + built separately and consumed directly by a
# standalone GstAudioDecoder. Gated by -Dwith_iamf and the presence of all
# three substream codecs (opus + fdk-aac + flac, the latter from aosp/ above).
# ---------------------------------------------------------------------------
iamf_opt    = get_option('with_iamf')
have_iamf   = false
libiamf_dep = []                         # safe placeholder when IAMF is off
if not iamf_opt.disabled()
  if opus_dep.found() and fdk_aac_dep.found() and flac_dep.found()
    iamf_up    = meson.current_source_dir() / 'iamf' / 'upstream'
    iamf_stamp = iamf_up / '.fetch.stamp'
    if not import('fs').exists(iamf_stamp)
      if fetch_opt.disabled()
        if iamf_opt.enabled()
          error('with_iamf=enabled but iamf/upstream/ is empty and aosp_fetch=disabled.')
        endif
      else
        message('Fetching libiamf ' + get_option('iamf_tag') + ' into ' + iamf_up + ' ...')
        iamf_fetch = run_command(python3,
          files('scripts/fetch_libiamf.py'),
          '--out', iamf_up,
          '--tag', get_option('iamf_tag'),
          check : false)
        if iamf_fetch.returncode() != 0
          message('libiamf fetch stdout:\n' + iamf_fetch.stdout())
          message('libiamf fetch stderr:\n' + iamf_fetch.stderr())
        endif
      endif
    endif
    if import('fs').exists(iamf_stamp)
      subdir('iamf')   # sets have_iamf = true, libiamf_dep
    elif iamf_opt.enabled()
      error('with_iamf=enabled but the libiamf fetch did not succeed (see logs above).')
    endif
  elif iamf_opt.enabled()
    error('with_iamf=enabled but need opus + fdk-aac + flac (one is missing).')
  endif
endif

subdir('src')       # the actual GStreamer plugin .so

if get_option('tests').allowed()
  subdir('tests')
endif

summary({
    'AOSP tag'        : aosp_tag,
    'libopus'         : opus_dep.found() ? opus_dep.version() : 'missing',
    'libfdk-aac'      : fdk_aac_dep.found() ? fdk_aac_dep.version() : 'missing',
    'system opus'     : get_option('with_system_opus'),
    'system fdk-aac'  : get_option('with_system_fdk_aac'),
    'IAMF (libiamf)'  : have_iamf ? ('enabled (' + get_option('iamf_tag') + ')') : 'disabled',
    'log level'       : get_option('logging'),
  }, section : 'gst-plugins-android')
