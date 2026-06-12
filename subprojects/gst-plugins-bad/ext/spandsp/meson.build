spandsp_sources = [
  'gstdtmfdetect.c',
  'gstdtmfdetect.h',
  'gstspandsp.c',
  'gstspanplc.c',
  'gstspanplc.h',
  'gsttonegeneratesrc.c',
  'gsttonegeneratesrc.h',
]

spandsp_headers = [
  'gstspanplc.h',
  'gstdtmfdetect.h',
  'gsttonegeneratesrc.h',
]

doc_sources = []
foreach s: spandsp_sources + spandsp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'spandsp': pathsep.join(doc_sources)
}

spandsp_dep = dependency('spandsp', version : '>= 0.0.6', required : get_option('spandsp'))

if spandsp_dep.found()
  spandsp_extra_args = []
  # https://github.com/freeswitch/spandsp/commit/3d5b276242d0940e3779bce14f183db2d8105e74 for plc_state_s
  if spandsp_dep.version().version_compare('>= 1.99.0')
    spandsp_extra_args += ['-DGST_INCLUDE_PRIVATE_PLC']
  endif

  gstspandsp = library('gstspandsp',
    spandsp_sources,
    c_args : gst_plugins_bad_args + cc.get_supported_arguments(['-Wno-undef']),
    include_directories : [configinc],
    dependencies : [gstvideo_dep, spandsp_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstspandsp]
endif
