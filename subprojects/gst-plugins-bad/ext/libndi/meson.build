ndi_sources = [
  'gstndi.c',
  'gstndisrc.c',
]

ndi_headers = [
  'gstndisrc.h',
]

doc_sources = []
foreach s: ndi_sources + ndi_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'ndi': pathsep.join(doc_sources)
}

ndi_option = get_option('libndi')
ndi_dep = dependency('libndi', version : '>= 0.1.0', required : ndi_option, allow_fallback: true)

if ndi_dep.found()
  gstndi = library('gstndi',
    ndi_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, ndi_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstndi]
endif
