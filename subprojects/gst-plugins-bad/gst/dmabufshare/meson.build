dmabufshare_sources = [
  'gstdmabufshare.c',
]

doc_sources = []
foreach s: dmabufshare_sources
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dmabufshare': pathsep.join(doc_sources)
}

if get_option('dmabufshare').disabled()
  subdir_done()
endif

gstdmabufshare = library('gstdmabufshare',
  dmabufshare_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstallocators_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdmabufshare]
