subparse_sources = [
  'gstssaparse.c',
  'gstsubparse.c',
  'gstsubparseelement.c',
  'gstsubparseplugin.c',
  'gstwebvttparse.c',
  'samiparse.c',
  'tmplayerparse.c',
  'mpl2parse.c',
  'qttextparse.c',
]

subparse_headers = [
  'gstcssparse.h',
  'gstwebvttparse.h',
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

# WebVTT CSS parsing via Rust
have_webvtt_css = false
css_parse_lib = []
have_rust = add_languages('rust', native : false, required : false)
if have_rust
  rustc = meson.get_compiler('rust')
  if rustc.get_id() in ['rustc', 'clippy-driver rustc']
    css_parse_rs = static_library('gst_cssparse', 'gstcssparse.rs',
      override_options : ['rust_std=2021'],
      rust_abi : 'c')
    css_parse_lib = [css_parse_rs]
    have_webvtt_css = true
    message('WebVTT CSS parser: Rust implementation enabled')
  else
    warning('WebVTT CSS parser requires rustc, found @0@'.format(rustc.get_id()))
  endif
else
  message('WebVTT CSS parser: Rust compiler not found, disabled')
endif

subparse_c_args = gst_plugins_base_args
if have_webvtt_css
  subparse_c_args += ['-DHAVE_WEBVTT_CSS']
  message('HAVE_WEBVTT_CSS defined')
else
  message('HAVE_WEBVTT_CSS not defined')
endif

gstsubparse = library('gstsubparse',
  subparse_sources,
  c_args: subparse_c_args,
  include_directories: [configinc, libsinc],
  dependencies: [gst_base_dep],
  link_with: css_parse_lib,
  install: true,
  install_dir: plugins_install_dir
)
plugins += [gstsubparse]