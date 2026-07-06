archive_sources = [
  'gstarchiveplugin.c',
  'gstzipfilesrc.c',
  'gstzipparser.c',
]

archive_headers = [
  'gstzipfilesrc.h',
  'gstzipparser.h',
]

doc_sources = []
foreach s: archive_sources + archive_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'archive': pathsep.join(doc_sources)
}

if get_option('archive').disabled()
  subdir_done()
endif

gstarchive = library('gstarchive',
  archive_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstarchive]
