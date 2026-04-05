add_languages('rust', native : false, required : true)
rustc = meson.get_compiler('rust')

rust_req = '1.77'
if not rustc.version().version_compare('>=' + rust_req)
  error('Requires least Rust @0@, found @1@'.format(rust_req, rustc.version()))
endif

typefindfunctions_extra_defs = []
typefindfunctions_extra_deps = []
if not get_option('gio-typefinder').disabled()
  typefindfunctions_extra_defs += ['-DHAVE_GIO']
  typefindfunctions_extra_deps += [gio_dep]
endif

typefindfunctions_sources = [
'gsttypefindfunctions.c',
'gsttypefindfunctionsdata.c',
'gsttypefindfunctionsplugin.c',
'gsttypefindfunctionsriff.c',
'gsttypefindfunctionsstartwith.c',
]

typefindfunctions_headers = [
  'gsttypefindfunctionsdata.h',
  'gsttypefindfunctionsplugin.h',
]

rust = import('rust')
typefindfunctions_bindgen_rs = rust.bindgen(
    input : 'bindgen.h',
    output : 'bindgen.rs',
    c_args : gst_plugins_base_args,
    dependencies : [gst_dep],
    args : ['--no-prepend-enum-name'],
)

typefindfunctions_rs = static_library('typefindfunctions_rs',
  structured_sources(
    [
      'rust/typefindfunctions.rs',
      'rust/helpers.rs',
    ],
    {
      '.': typefindfunctions_bindgen_rs,
    },
  ),
  override_options : ['rust_std=2021'],
  rust_abi : 'c',
)

doc_sources = []
foreach s: typefindfunctions_sources + typefindfunctions_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'typefindfunctions': pathsep.join(doc_sources)
}

if get_option('typefind').disabled()
  subdir_done()
endif

gsttypefind = library('gsttypefindfunctions',
  typefindfunctions_sources,
  c_args : gst_plugins_base_args + typefindfunctions_extra_defs,
  include_directories: [configinc, libsinc],
  dependencies : [pbutils_dep, gst_base_dep, typefindfunctions_extra_deps],
  link_with : typefindfunctions_rs,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsttypefind]
