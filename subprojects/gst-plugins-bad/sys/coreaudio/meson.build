coreaudio_sources = [
  'gstcoreaudiosink.c',
  'gstcoreaudiosrc.c',
  'gstcoreaudiocontext.c',
  'gstcoreaudioringbuf.c',
  'gstcoreaudioutils.c',
  'gstcoreaudiodeviceprovider.c',
  'plugin.c',
]

coreaudio_headers = [
  'gstcoreaudiosink.h',
  'gstcoreaudiosrc.h',
  'gstcoreaudioutils.h',
  'gstcoreaudiocontext.h',
  'gstcoreaudioringbuf.h',
  'gstcoreaudiodeviceprovider.h',
]

doc_sources = []
foreach s: coreaudio_sources + coreaudio_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'coreaudio': pathsep.join(doc_sources)
}

coreaudio_option = get_option('coreaudio')
if coreaudio_option.disabled() or not ['darwin'].contains(host_system)
  subdir_done()
endif

modules = ['CoreAudio', 'AudioUnit', 'CoreServices']
coreaudio_dep = dependency('appleframeworks', modules : modules)

gstcoreaudio = library('gstcoreaudio',
  coreaudio_sources,
  c_args : gst_plugins_bad_args,
  objc_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstaudio_dep, gstpbutils_dep, coreaudio_dep],
  install : true,
  install_dir : plugins_install_dir)
plugins += [gstcoreaudio]
