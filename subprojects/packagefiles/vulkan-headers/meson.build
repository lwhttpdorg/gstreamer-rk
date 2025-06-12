project(
  'vulkan-headers',
  'c',
  version: '1.4.317',
  license: 'Apache-2.0',
  meson_version: '>=0.54.0',
)

# Headers-only library for Vulkan
vulkan_headers_inc = include_directories('include')

# Create a dependency that only provides headers
vulkan_headers_dep = declare_dependency(
  include_directories: vulkan_headers_inc,
  version: meson.project_version(),
)
