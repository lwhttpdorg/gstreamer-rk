movpkg_sources = [
  'movpkg/gstmovpkgdemux.c',
  'movpkg/gstmovpkgdemux-stream.c',
  'movpkg/movpkg-moviepackage.c',
  'movpkg/movpkg-hlsmoviepackage.c',
  'movpkg/movpkg-streaminfo.c',
  'movpkg/gstmovpkgelement.c',
]

movpkg_headers = [
]

movpkg_json_dep = dependency('json-glib-1.0', version : '>=1.6.6', fallback : ['json-glib', 'json_glib_dep'], required : false)

if movpkg_json_dep.found()
  movpkg_sources += ['movpkg/movpkg-chapters-json.c']
endif

movpkg_cargs = []

movpkg_dep = declare_dependency(include_directories : include_directories('.'))
