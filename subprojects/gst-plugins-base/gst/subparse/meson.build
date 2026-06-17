subparse_sources = [
  'gstssaparse.c',
  'gstsubparse.c',
  'gstsubparseelement.c',
  'gstsubparseplugin.c',
  'samiparse.c',
  'tmplayerparse.c',
  'mpl2parse.c',
  'qttextparse.c',
]
subparse_defines = []
subparse_optional_deps = []

subparse_uchardet_dep = dependency('uchardet', required : get_option('subparse-uchardet'))
if subparse_uchardet_dep.found()
  subparse_defines += '-DHAVE_UCHARDET'
  subparse_optional_deps += subparse_uchardet_dep
endif

subparse_headers = [
  'qttextparse.h',
  'samiparse.h',
  'gstsubparseelements.h',
  'gstssaparse.h',
  'mpl2parse.h',
  'tmplayerparse.h',
  'gstsubparse.h',
]

doc_sources = []
foreach s: subparse_sources + subparse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'subparse': pathsep.join(doc_sources)
}

if get_option('subparse').disabled()
  subdir_done()
endif

gstsubparse = library('gstsubparse',
  subparse_sources,
  c_args : gst_plugins_base_args + subparse_defines,
  include_directories: [configinc, libsinc],
  dependencies : [gst_base_dep] + subparse_optional_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsubparse]
