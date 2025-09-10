videoframescheduler_sources = [
  'gstvideoframescheduler.c'
]

gstvideoframescheduler = library('gstvideoframescheduler',
  videoframescheduler_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideoframescheduler]
