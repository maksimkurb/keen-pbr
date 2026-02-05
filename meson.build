project(
  'keen-pbr3',
  'cpp',
  version: '3.0.0',
  default_options: [
    'cpp_std=c++20',
    'buildtype=minsize',
    'b_lto=true',
    'warning_level=2',
  ],
)

# Size optimization flags
add_project_arguments(
  '-Os',
  '-ffunction-sections',
  '-fdata-sections',
  language: 'cpp',
)

add_project_link_arguments(
  '-Wl,--gc-sections',
  language: 'cpp',
)

# Dependencies
curl_dep = dependency('libcurl')
json_dep = dependency('nlohmann_json')
libnl_dep = dependency('libnl-3.0')
libnl_route_dep = dependency('libnl-route-3.0')
mbedtls_dep = dependency('mbedtls')

deps = [curl_dep, json_dep, libnl_dep, libnl_route_dep, mbedtls_dep]

# Optional REST API
if get_option('with_api')
  httplib_dep = dependency('cpp-httplib')
  deps += httplib_dep
  add_project_arguments('-DWITH_API', language: 'cpp')
endif

# Include directories
inc = include_directories('include')

# Sources will be added as stories are implemented
sources = [
  'src/http/http_client.cpp',
  'src/config/config.cpp',
]

# Executable target (will be populated with sources in later stories)
# executable(
#   'keen-pbr3',
#   sources,
#   include_directories: inc,
#   dependencies: deps,
#   install: true,
# )
