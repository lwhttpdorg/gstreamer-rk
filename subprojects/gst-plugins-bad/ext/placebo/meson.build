placebo_sources = [
  'gstlibplacebo.c',
  'gstplacebo.c',
  'placeboapi.c',
]

placebo_headers = [
  'gstplacebo.h',
  'placeboapi.h',
]

doc_sources = []
foreach s: placebo_sources + placebo_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'placebo': pathsep.join(doc_sources)
}

if (get_option('gl').disabled() or not gstgl_dep.found()) and (get_option('vulkan').disabled() or not gstvulkan_dep.found())
  subdir_done()
endif

api_deps = []
if gstgl_dep.found() and not get_option('gl').disabled()
  api_deps += [gstgl_dep.found()]
  placebo_sources += ['placebogl.c']
  placebo_headers += ['placebogl.h']
endif
if gstvulkan_dep.found() and not get_option('vulkan').disabled()
  api_deps += [gstvulkan_dep.found()]
  placebo_sources += ['placebovulkan.c']
  placebo_headers += ['placebovulkan.h']
endif

gstplacebo = library('gstplacebo',
  placebo_sources,
  c_args : gst_plugins_bad_args,
  link_args : noseh_link_args,
  include_directories : [configinc],
  dependencies : [libplacebo_dep, video_dep,
                  gst_base_dep, gst_controller_dep, libm] + api_deps,
  install : true,
  install_dir : plugins_install_dir)

plugins += [gstplacebo]
