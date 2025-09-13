vlndi_sources = [
  'gstvlndi.c',
  'gstvlndisrc.c',
]

vlndi_headers = [
  'gstvlndisrc.h',
]

doc_sources = []
foreach s: vlndi_sources + vlndi_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'vlndi': pathsep.join(doc_sources)
}

vlndi_option = get_option('libndi')
vlndi_dep = dependency('libndi', version : '>= 0.1.0', required : vlndi_option, allow_fallback: true)

if vlndi_dep.found()
  gstvlndi = library('gstvlndi',
    vlndi_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, vlndi_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstvlndi]
endif
