# ─── tests/ ────────────────────────────────────────────────────────────────
# Lightweight self-tests that don't require sample media:
#   - test_c2_register : loads the .so and checks both elements register.
#   - test_c2_standalone : creates each C2 component, queries traits, drives
#     a tiny dummy Work item. Validates the porting shim more than the codecs.

test_c2_register = executable('test-c2-register',
  ['test_c2_register.c'],
  dependencies : [gst_dep, gst_audio_dep],
  build_by_default : false,
)
test('register', test_c2_register,
  args : ['--gst-plugin-path', meson.global_build_root() / 'src'],
  is_parallel : false,
)

test_c2_standalone = executable('test-c2-standalone',
  ['test_c2_standalone.cc'],
  dependencies : [libc2_dep],
  build_by_default : false,
)
test('standalone', test_c2_standalone, is_parallel : false)
