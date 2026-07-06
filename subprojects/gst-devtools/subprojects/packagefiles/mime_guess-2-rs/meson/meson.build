# build.rs replacement for mime_guess. Meson does not run build.rs and on
# stable rustc cannot pass OUT_DIR, so rather than reimplement the reverse
# mime<->ext map generation we compile and run the crate's own build.rs (its
# only dep, unicase, is compiled first) with the feature set Meson resolved.
# The output is faithful by construction; it lands next to the source so the
# (redirected) relative include! finds it. Runs at configure since the crate is
# compiled in place. `features` is provided by the cargo module.
gen_mime_cmd = [
  find_program('run-build-rs.py'),
  '--crate-root', meson.current_source_dir() / '..',
  '--subprojects-dir', meson.current_source_dir() / '..' / '..',
  '--out', meson.current_source_dir() / '..' / 'src' / 'mime_types_generated.rs',
]
foreach f : features
  gen_mime_cmd += ['--feature', f]
endforeach
# Use the rust compiler Meson configured, not whatever rustc is on PATH. After
# `--` so the compiler's own flags (e.g. `-C linker=cc`) are passed through.
gen_mime_cmd += ['--'] + meson.get_compiler('rust').cmd_array()
run_command(gen_mime_cmd, check: true)
