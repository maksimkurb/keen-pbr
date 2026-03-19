cmake_minimum_required(VERSION 3.14)
project(keen-pbr3 VERSION 3.0.0 LANGUAGES CXX)

if(DEFINED CMAKE_CXX_STANDARD AND NOT CMAKE_CXX_STANDARD STREQUAL "17")
  message(FATAL_ERROR "keen-pbr3 requires CMAKE_CXX_STANDARD=17.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Size optimization
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

# Export symbols for backtrace() stack traces on crash
add_link_options(-rdynamic)

# Options
option(WITH_API "Build with REST API support (cpp-httplib)" ON)

# Dependencies
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNL REQUIRED IMPORTED_TARGET libnl-3.0)
pkg_check_modules(LIBNL_ROUTE REQUIRED IMPORTED_TARGET libnl-route-3.0)

# nlohmann/json (bundled as git submodule)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(third_party/nlohmann_json)

# fmt (bundled as git submodule)
set(FMT_DOC OFF CACHE INTERNAL "")
set(FMT_INSTALL OFF CACHE INTERNAL "")
set(FMT_TEST OFF CACHE INTERNAL "")
set(FMT_FUZZ OFF CACHE INTERNAL "")
set(FMT_CUDA_TEST OFF CACHE INTERNAL "")
add_subdirectory(third_party/fmt)

# cpp-httplib (bundled as git submodule, only if WITH_API — no TLS needed)
if(WITH_API)
  set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE INTERNAL "")
  add_subdirectory(third_party/cpp-httplib)
endif()

# Sources
set(SOURCES
  src/log/logger.cpp
  src/http/http_client.cpp
  src/config/config.cpp
  src/config/routing_state.cpp
  src/config/list_parser.cpp
  src/lists/ipset.cpp
  src/lists/list_streamer.cpp
  src/cache/cache_manager.cpp
  src/cmd/status.cpp
  src/cmd/test_routing.cpp
  src/routing/target.cpp
  src/routing/netlink.cpp
  src/routing/route_table.cpp
  src/routing/policy_rule.cpp
  src/routing/routing_verifier.cpp
  src/health/circuit_breaker.cpp
  src/health/url_tester.cpp
  src/health/routing_health_checker.cpp
  src/routing/urltest_manager.cpp
  src/routing/firewall_state.cpp
  src/firewall/firewall.cpp
  src/firewall/iptables.cpp
  src/firewall/nftables.cpp
  src/firewall/ipset_restore_pipe.cpp
  src/firewall/nft_batch_pipe.cpp
  src/firewall/firewall_verifier.cpp
  src/firewall/iptables_verifier.cpp
  src/firewall/nftables_verifier.cpp
  src/dns/dns_server.cpp
  src/dns/dns_probe_server.cpp
  src/dns/dns_router.cpp
  src/dns/dnsmasq_gen.cpp
  src/daemon/daemon.cpp
  src/daemon/scheduler.cpp
  src/main.cpp
)

if(WITH_API)
  list(APPEND SOURCES
    src/api/server.cpp
    src/api/handlers.cpp
    src/api/sse_broadcaster.cpp
    src/api/handler_health_service.cpp
    src/api/handler_reload.cpp
    src/api/handler_config.cpp
    src/api/handler_health_routing.cpp
    src/api/handler_test_routing.cpp
    src/api/handler_dns_test.cpp
  )
endif()

# Executable
add_executable(keen-pbr3 ${SOURCES})
set_target_properties(keen-pbr3 PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
target_include_directories(keen-pbr3 PRIVATE include)
target_link_libraries(keen-pbr3 PRIVATE
  CURL::libcurl
  PkgConfig::LIBNL
  PkgConfig::LIBNL_ROUTE
  fmt::fmt-header-only
  nlohmann_json::nlohmann_json
)

if(WITH_API)
  target_compile_definitions(keen-pbr3 PRIVATE WITH_API)
  target_link_libraries(keen-pbr3 PRIVATE httplib::httplib)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(keen-pbr3 PRIVATE stdc++fs)
endif()

install(TARGETS keen-pbr3 DESTINATION bin)

# Unit tests (optional, requires doctest submodule)
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
