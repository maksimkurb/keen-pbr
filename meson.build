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

# Source files
sources = [
  'src/http/http_client.cpp',
  'src/config/config.cpp',
  'src/config/list_parser.cpp',
  'src/lists/ipset.cpp',
  'src/lists/list_manager.cpp',
  'src/routing/target.cpp',
  'src/routing/netlink.cpp',
  'src/routing/route_table.cpp',
  'src/routing/policy_rule.cpp',
  'src/health/health_checker.cpp',
  'src/health/circuit_breaker.cpp',
  'src/firewall/firewall.cpp',
  'src/firewall/iptables.cpp',
  'src/firewall/nftables.cpp',
  'src/dns/dns_server.cpp',
  'src/dns/dns_router.cpp',
  'src/dns/dnsmasq_gen.cpp',
  'src/daemon/daemon.cpp',
  'src/daemon/scheduler.cpp',
  'src/main.cpp',
]

# Optional API sources (guarded by #ifdef WITH_API in source)
if get_option('with_api')
  sources += 'src/api/server.cpp'
  sources += 'src/api/handlers.cpp'
endif

# Static linking for embedded targets (cross-compilation)
if meson.is_cross_build()
  add_project_link_arguments('-static', language: 'cpp')
endif

# Executable target
executable(
  'keen-pbr3',
  sources,
  include_directories: inc,
  dependencies: deps,
  install: true,
)
