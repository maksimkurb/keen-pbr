cmake_minimum_required(VERSION 3.14)
project(keen-pbr VERSION 3.0.0 LANGUAGES CXX)

if(DEFINED CMAKE_CXX_STANDARD AND NOT CMAKE_CXX_STANDARD STREQUAL "17")
  message(FATAL_ERROR "keen-pbr requires CMAKE_CXX_STANDARD=17.")
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
option(USE_KEENETIC_API "Enable Keenetic RCI integration (DNS server type: keenetic)" OFF)
set(KEEN_PBR_DEFAULT_CONFIG_PATH "/etc/keen-pbr/config.json" CACHE STRING
  "Default config file path used by keen-pbr when --config is not passed")
set(KEEN_PBR_FRONTEND_ROOT "/usr/share/keen-pbr/frontend" CACHE STRING
  "Default frontend static root served by API (SPA assets)")

# Dependencies
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_library(LIBRESOLV resolv)
find_library(LIBCRYPT crypt)
if(LIBRESOLV)
  message(STATUS "Found libresolv: ${LIBRESOLV}")
else()
  message(STATUS "libresolv not found; relying on libc resolver symbols")
endif()
pkg_check_modules(LIBNL REQUIRED IMPORTED_TARGET libnl-3.0)
pkg_check_modules(LIBNL_ROUTE REQUIRED IMPORTED_TARGET libnl-route-3.0)
set(HTTPLIB_ANL_LIB "")

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
  find_library(HTTPLIB_ANL_LIB anl)
  if(HTTPLIB_ANL_LIB)
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO ON CACHE BOOL "" FORCE)
  else()
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
  endif()
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
  src/lists/list_set_usage.cpp
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
  src/health/runtime_outbound_state.cpp
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
  src/dns/keenetic_dns.cpp
  src/dns/dns_txt_client.cpp
  src/dns/dns_probe_server.cpp
  src/dns/dns_router.cpp
  src/dns/dnsmasq_gen.cpp
  src/daemon/daemon.cpp
  src/daemon/system_resolver_hook.cpp
  src/daemon/scheduler.cpp
  src/main.cpp
)

if(WITH_API)
  list(APPEND SOURCES
    src/api/server.cpp
    src/api/auth.cpp
    src/api/handlers.cpp
    src/api/sse_broadcaster.cpp
    src/api/handler_health_service.cpp
    src/api/handler_reload.cpp
    src/api/handler_config.cpp
    src/api/handler_health_routing.cpp
    src/api/handler_runtime_outbounds.cpp
    src/api/handler_test_routing.cpp
    src/api/handler_dns_test.cpp
  )
endif()

# Executable
add_executable(keen-pbr ${SOURCES})
set_target_properties(keen-pbr PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
target_include_directories(keen-pbr PRIVATE include)
target_compile_definitions(keen-pbr PRIVATE
  KEEN_PBR_DEFAULT_CONFIG_PATH="${KEEN_PBR_DEFAULT_CONFIG_PATH}"
  KEEN_PBR_FRONTEND_ROOT="${KEEN_PBR_FRONTEND_ROOT}"
)
if(USE_KEENETIC_API)
  target_compile_definitions(keen-pbr PRIVATE USE_KEENETIC_API)
endif()
target_link_libraries(keen-pbr PRIVATE
  CURL::libcurl
  PkgConfig::LIBNL
  PkgConfig::LIBNL_ROUTE
  Threads::Threads
  fmt::fmt-header-only
  nlohmann_json::nlohmann_json
)

if(LIBRESOLV)
  target_link_libraries(keen-pbr PRIVATE ${LIBRESOLV})
endif()

if(WITH_API)
  target_compile_definitions(keen-pbr PRIVATE WITH_API)
  target_link_libraries(keen-pbr PRIVATE httplib::httplib)
  if(HTTPLIB_ANL_LIB)
    target_link_libraries(keen-pbr PRIVATE ${HTTPLIB_ANL_LIB})
  endif()
  if(LIBCRYPT)
    target_link_libraries(keen-pbr PRIVATE ${LIBCRYPT})
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(keen-pbr PRIVATE stdc++fs)
endif()

install(TARGETS keen-pbr DESTINATION bin)

# Unit tests (optional, requires doctest submodule)
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
