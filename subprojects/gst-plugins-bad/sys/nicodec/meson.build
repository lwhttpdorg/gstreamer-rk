if host_machine.system() == 'windows'
  ws2_32_dep = cc.find_library('ws2_32', required : true)
  platform_deps = [ws2_32_dep]
else
  platform_deps = []
endif

ni_plugins_args = ['-DHAVE_CONFIG_H', '-DGST_USE_UNSTABLE_API']

niquadra_sources =[
    'gstniquadra.c',
    'gstniquadracontext.c',
    'gstniquadradec.c',
    'gstniquadrah264dec.c',
    'gstniquadrah265dec.c',
    'gstniquadravp9dec.c',
    'gstniquadrajpegdec.c',
    'gstniquadraenc.c',
    'gstniquadrah264enc.c',
    'gstniquadrah265enc.c',
    'gstniquadrajpegenc.c',
    'gstniquadraav1enc.c',
    'gstniquadrautils.c',
    'gstniquadrahwdownload.c',
    'gstniquadrahwupload.c',
    'gstniquadramemory.c',
    'gstniquadrascale.c',
    'gstnieval.c',
    'gstnimetadata.c',
    'gstniquadrainterleave.c',
    'gstniquadrabaseconvert.c',
    ]

niquadra_dep = []

niquadra_option = get_option('nicodec')
if niquadra_option.disabled()
  subdir_done()
endif

xcoder_dep = dependency('xcoder', required: get_option('nicodec'))
if xcoder_dep.found()
  xcoder_incdir = xcoder_dep.get_variable('includedir')
  xcoder_inc = []
elif get_option('nicodec').enabled()
  error('niquadra plugin enabled but NI xcoder not found: consider setting PKG_CONFIG_PATH')
  subdir_done()
else
  subdir_done()
endif

if xcoder_dep.found()
  gstniquadratag = library('gstniquadra',
    niquadra_sources,
    c_args : gst_plugins_bad_args + ni_plugins_args,
    include_directories : [configinc, xcoder_inc],
    dependencies : [gstbase_dep, gstapp_dep, gstvideo_dep, gstpbutils_dep, gstallocators_dep, gstcodecparsers_dep, xcoder_dep] + platform_deps,
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstniquadratag, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstniquadratag]
endif
