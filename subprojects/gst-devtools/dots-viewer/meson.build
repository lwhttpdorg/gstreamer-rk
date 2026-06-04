dots_viewer_opt = get_option('dots_viewer')

# rust.workspace() (used below) requires Meson >= 1.11.
if not meson.version().version_compare('>= 1.11')
  if dots_viewer_opt.enabled()
    error('dots-viewer requires Meson >= 1.11 (rust.workspace())')
  endif
  warning('dots-viewer disabled: requires Meson >= 1.11 (rust.workspace())')
  subdir_done()
endif

if not add_languages('rust', required: dots_viewer_opt, native: false)
  subdir_done()
endif

rust = import('rust')
ws = rust.workspace()
pkg = ws.package('gst-dots-viewer')

gen_resources = find_program('meson' / 'gen-resources.py')
generated_rs = custom_target(
  'generated.rs',
  output: 'generated.rs',
  command: [gen_resources, meson.current_source_dir() / 'static', '@OUTPUT@'],
)

version_rs = configure_file(
  input: 'meson' / 'version.rs.in',
  output: 'version.rs',
  configuration: {'VERSION': pkg.version()},
)

# structured_sources lays the crate out for rustc: every file is placed by
# basename at the root, so main.rs (the entry) finds protocol.rs, generated.rs
# and version.rs by their relative include!/mod paths. pkg.executable() pulls
# the dependencies, rust_dependency_map, rustc args and edition (rust_std) from
# the Cargo manifest; only the meson-build cfg is added on top.
sources = structured_sources(
  ['src' / 'main.rs', 'src' / 'protocol.rs', generated_rs, version_rs],
)

gst_dots_viewer = pkg.executable(
  'gst-dots-viewer',
  sources,
  rust_args: ['--cfg', 'dots_meson_build', '--check-cfg', 'cfg(dots_meson_build)'],
  install: true,
)

devenv = environment()
devenv.append('PATH', meson.current_build_dir())
meson.add_devenv(devenv)
