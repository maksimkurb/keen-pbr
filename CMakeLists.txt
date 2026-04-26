cmake_minimum_required(VERSION 3.14)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.mk" KEEN_PBR_VERSION_LINES
  REGEX "^KEEN_PBR_(VERSION|RELEASE)=")

foreach(line IN LISTS KEEN_PBR_VERSION_LINES)
  if(line MATCHES "^KEEN_PBR_VERSION=([0-9]+\\.[0-9]+\\.[0-9]+)$")
    set(KEEN_PBR_VERSION_FROM_FILE "${CMAKE_MATCH_1}")
  elseif(line MATCHES "^KEEN_PBR_RELEASE=([0-9]+)$")
    set(KEEN_PBR_RELEASE_FROM_FILE "${CMAKE_MATCH_1}")
  endif()
endforeach()

if(NOT DEFINED KEEN_PBR_VERSION OR KEEN_PBR_VERSION STREQUAL "")
  set(KEEN_PBR_VERSION "${KEEN_PBR_VERSION_FROM_FILE}")
endif()

if(DEFINED ENV{KEEN_PBR_RELEASE_OVERRIDE} AND
   NOT "$ENV{KEEN_PBR_RELEASE_OVERRIDE}" STREQUAL "")
  set(KEEN_PBR_RELEASE "$ENV{KEEN_PBR_RELEASE_OVERRIDE}")
endif()

if(NOT DEFINED KEEN_PBR_RELEASE OR KEEN_PBR_RELEASE STREQUAL "")
  set(KEEN_PBR_RELEASE "${KEEN_PBR_RELEASE_FROM_FILE}")
endif()

if(NOT DEFINED KEEN_PBR_VERSION OR KEEN_PBR_VERSION STREQUAL "" OR
   NOT DEFINED KEEN_PBR_RELEASE OR KEEN_PBR_RELEASE STREQUAL "")
  message(FATAL_ERROR "Failed to read KEEN_PBR_VERSION/KEEN_PBR_RELEASE from version.mk")
endif()

if(NOT KEEN_PBR_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "KEEN_PBR_VERSION must use semantic version format X.Y.Z")
endif()

set(KEEN_PBR_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(KEEN_PBR_VERSION_MINOR "${CMAKE_MATCH_2}")
set(KEEN_PBR_VERSION_PATCH "${CMAKE_MATCH_3}")

if(NOT KEEN_PBR_RELEASE MATCHES "^[0-9]+$")
  message(FATAL_ERROR "KEEN_PBR_RELEASE must contain only digits")
endif()

project(keen-pbr VERSION ${KEEN_PBR_VERSION} LANGUAGES CXX)

include(GNUInstallDirs)

if(DEFINED CMAKE_CXX_STANDARD AND NOT CMAKE_CXX_STANDARD STREQUAL "17")
  message(FATAL_ERROR "keen-pbr requires CMAKE_CXX_STANDARD=17.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Size optimization
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

# Export symbols so crash diagnostics can recover names in non-signal-safe paths.
add_link_options(-rdynamic)

# Enrich nlohmann::json exception messages with JSON Pointer context.
add_compile_definitions(JSON_DIAGNOSTICS=1)

# Options
option(WITH_API "Build with REST API support (cpp-httplib)" ON)
option(USE_KEENETIC_API "Enable Keenetic RCI integration (DNS server type: keenetic)" OFF)
option(ENABLE_THREAD_SAFETY_ANALYSIS "Enable Clang thread-safety diagnostics for project targets" OFF)
option(KEEN_PBR_STATIC_LIBUNWIND "Prefer linking libunwind statically when a static archive is available" ON)
set(KEEN_PBR_DEFAULT_CONFIG_PATH "${CMAKE_INSTALL_FULL_SYSCONFDIR}/keen-pbr/config.json" CACHE STRING
  "Default config file path used by keen-pbr when --config is not passed")
set(KEEN_PBR_FRONTEND_ROOT "${CMAKE_INSTALL_FULL_DATADIR}/keen-pbr/frontend" CACHE STRING
  "Default frontend static root served by API (SPA assets)")
set(KEEN_PBR_SYSTEM_RESOLVER_HOOK "${CMAKE_INSTALL_FULL_LIBDIR}/keen-pbr/dnsmasq.sh" CACHE STRING
  "System resolver hook command path compiled into keen-pbr")
set(KEEN_PBR_TARGET_OS "linux" CACHE STRING
  "Compile-time target OS identity exposed via /api/health/service (for example: openwrt, keenetic, debian)")

# Dependencies
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBCURL IMPORTED_TARGET libcurl)
if(TARGET PkgConfig::LIBCURL)
  # Prefer the distro-provided libcurl discovered via pkg-config so CI does not
  # accidentally link against a runner-local /usr/local curl build.
  set(KEEN_PBR_CURL_TARGET PkgConfig::LIBCURL)
  message(STATUS "Using pkg-config libcurl target: ${LIBCURL_LINK_LIBRARIES}")
else()
  find_package(CURL REQUIRED)
  set(KEEN_PBR_CURL_TARGET CURL::libcurl)
  message(STATUS "Using CMake libcurl target: ${CURL_LIBRARY_RELEASE}")
endif()
add_library(keen_pbr_libunwind INTERFACE)
set(_keen_pbr_saved_find_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
set(_keen_pbr_prefer_static_libunwind ${KEEN_PBR_STATIC_LIBUNWIND})
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GNU libstdc++ exception unwinding can break when a static libunwind archive
  # is linked into the final executable. Prefer the shared library when available.
  set(_keen_pbr_prefer_static_libunwind OFF)
endif()
if(_keen_pbr_prefer_static_libunwind)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so" ".so.8")
endif()
unset(LIBUNWIND_LIBRARY CACHE)
unset(LIBUNWIND_INCLUDE_DIR CACHE)
unset(LIBUNWIND CACHE)
unset(LIBUNWIND_INCLUDE_DIRS CACHE)
set(_keen_pbr_libunwind_include_hints
  "$ENV{STAGING_DIR}/usr/include"
  "$ENV{STAGING_DIR}/include"
)
set(_keen_pbr_libunwind_library_hints
  "$ENV{STAGING_DIR}/usr/lib"
  "$ENV{STAGING_DIR}/lib"
)
find_path(LIBUNWIND_INCLUDE_DIR
  NAMES libunwind.h libunwind/libunwind.h
  HINTS ${_keen_pbr_libunwind_include_hints}
  PATH_SUFFIXES libunwind
)
find_library(LIBUNWIND_LIBRARY
  NAMES unwind libunwind libunwind.so.8
  HINTS ${_keen_pbr_libunwind_library_hints}
)
set(KEEN_PBR_HAS_LIBUNWIND OFF)
if(LIBUNWIND_INCLUDE_DIR AND LIBUNWIND_LIBRARY)
  set(KEEN_PBR_HAS_LIBUNWIND ON)
  get_filename_component(LIBUNWIND_LIBRARY_DIR "${LIBUNWIND_LIBRARY}" DIRECTORY)
  list(APPEND CMAKE_INCLUDE_PATH "${LIBUNWIND_INCLUDE_DIR}")
  list(APPEND CMAKE_LIBRARY_PATH "${LIBUNWIND_LIBRARY_DIR}")
  set(LIBUNWIND "${LIBUNWIND_LIBRARY}" CACHE FILEPATH "libunwind library path shared with cpptrace" FORCE)
  set(LIBUNWIND_INCLUDE_DIRS "${LIBUNWIND_INCLUDE_DIR}" CACHE PATH "libunwind include path shared with cpptrace" FORCE)
endif()
find_library(LIBRESOLV resolv)
find_library(LIBATOMIC atomic)
if(NOT LIBRESOLV)
  message(FATAL_ERROR "libresolv is required for keen-pbr DNS resolution support")
endif()
message(STATUS "Found libresolv: ${LIBRESOLV}")
if(LIBATOMIC)
  message(STATUS "Found libatomic: ${LIBATOMIC}")
else()
  message(STATUS "libatomic not found; relying on toolchain default atomic support")
endif()
pkg_check_modules(LIBNL REQUIRED IMPORTED_TARGET libnl-3.0)
pkg_check_modules(LIBNL_ROUTE REQUIRED IMPORTED_TARGET libnl-route-3.0)
set(HTTPLIB_ANL_LIB "")

if(KEEN_PBR_HAS_LIBUNWIND)
  target_include_directories(keen_pbr_libunwind INTERFACE "${LIBUNWIND_INCLUDE_DIR}")
  target_link_libraries(keen_pbr_libunwind INTERFACE "${LIBUNWIND_LIBRARY}")
  message(STATUS "Using libunwind library: ${LIBUNWIND_LIBRARY}")
else()
  message(STATUS "libunwind headers/library not found; falling back to compiler _Unwind backend")
endif()

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

# cpptrace (bundled as git submodule, crash diagnostics only)
set(CPPTRACE_BUILD_SHARED OFF CACHE INTERNAL "")
set(CPPTRACE_INSTALL OFF CACHE INTERNAL "")
set(CPPTRACE_UNWIND_WITH_LIBUNWIND ${KEEN_PBR_HAS_LIBUNWIND} CACHE BOOL "" FORCE)
if(KEEN_PBR_HAS_LIBUNWIND)
  set(CPPTRACE_UNWIND_WITH_UNWIND OFF CACHE BOOL "" FORCE)
else()
  set(CPPTRACE_UNWIND_WITH_UNWIND ON CACHE BOOL "" FORCE)
endif()
set(CPPTRACE_UNWIND_WITH_EXECINFO OFF CACHE BOOL "" FORCE)
set(CPPTRACE_GET_SYMBOLS_WITH_LIBDL ON CACHE BOOL "" FORCE)
set(CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF CACHE BOOL "" FORCE)
set(CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cpptrace EXCLUDE_FROM_ALL)
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_keen_pbr_saved_find_library_suffixes}")

# cpp-httplib (bundled as git submodule, only if WITH_API — no TLS needed)
if(WITH_API)
  set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE INTERNAL "")
  set(HTTPLIB_INSTALL OFF CACHE INTERNAL "")
  find_library(HTTPLIB_ANL_LIB anl)
  if(HTTPLIB_ANL_LIB)
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO ON CACHE BOOL "" FORCE)
  else()
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(third_party/cpp-httplib)
endif()

add_library(keen_pbr_project_options INTERFACE)
if(ENABLE_THREAD_SAFETY_ANALYSIS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(keen_pbr_project_options INTERFACE
      -Wthread-safety
    )
  else()
    message(WARNING "ENABLE_THREAD_SAFETY_ANALYSIS is only supported with Clang; no extra diagnostics will be added.")
  endif()
endif()

function(keen_pbr_apply_project_options target_name)
  target_link_libraries(${target_name} PRIVATE keen_pbr_project_options)
endfunction()

# Sources
set(SOURCES
  src/log/logger.cpp
  src/log/trace.cpp
  src/http/http_client.cpp
  src/crash/crash_diagnostics.cpp
  src/config/config.cpp
  src/config/routing_state.cpp
  src/config/list_parser.cpp
  src/lists/ipset.cpp
  src/lists/kernel_set_tester.cpp
  src/lists/list_streamer.cpp
  src/lists/list_set_usage.cpp
  src/cache/cache_manager.cpp
  src/cmd/status.cpp
  src/cmd/test_routing.cpp
  src/routing/target.cpp
  src/routing/netlink.cpp
  src/routing/interface_monitor.cpp
  src/routing/route_table.cpp
  src/routing/policy_rule.cpp
  src/routing/routing_verifier.cpp
  src/health/circuit_breaker.cpp
  src/health/url_tester.cpp
  src/health/routing_health_checker.cpp
  src/health/runtime_outbound_state.cpp
  src/health/runtime_interface_inventory.cpp
  src/routing/urltest_manager.cpp
  src/routing/firewall_state.cpp
  src/firewall/firewall.cpp
  src/firewall/firewall_runtime.cpp
  src/firewall/port_spec_util.cpp
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
  src/daemon/config_store.cpp
  src/daemon/daemon_core.cpp
  src/daemon/daemon_runtime.cpp
  src/daemon/daemon_resolver.cpp
  src/daemon/daemon_api.cpp
  src/daemon/daemon_dns_probe.cpp
  src/daemon/list_service.cpp
  src/daemon/resolver_health.cpp
  src/daemon/runtime_state_store.cpp
  src/daemon/system_resolver_hook.cpp
  src/daemon/scheduler.cpp
  src/util/blocking_executor.cpp
  src/util/firewall_backend_utils.cpp
  src/util/system_info.cpp
  src/util/traced_mutex.cpp
  src/main.cpp
)

if(WITH_API)
  list(APPEND SOURCES
    src/api/server.cpp
    src/api/handlers.cpp
    src/api/sse_broadcaster.cpp
    src/api/handler_health_service.cpp
    src/api/handler_reload.cpp
    src/api/handler_lists_refresh.cpp
    src/api/handler_config.cpp
    src/api/handler_health_routing.cpp
    src/api/handler_runtime_interfaces.cpp
    src/api/handler_runtime_outbounds.cpp
    src/api/handler_test_routing.cpp
    src/api/handler_dns_test.cpp
  )
endif()

set(KEEN_PBR_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/keen-pbr/version.hpp.in"
  "${KEEN_PBR_GENERATED_INCLUDE_DIR}/keen-pbr/version.hpp"
  @ONLY
)

# Executable
add_executable(keen-pbr ${SOURCES})
set_target_properties(keen-pbr PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
target_include_directories(keen-pbr PRIVATE
  "${KEEN_PBR_GENERATED_INCLUDE_DIR}"
  include
  src
)
target_compile_definitions(keen-pbr PRIVATE
  KEEN_PBR_DEFAULT_CONFIG_PATH="${KEEN_PBR_DEFAULT_CONFIG_PATH}"
  KEEN_PBR_FRONTEND_ROOT="${KEEN_PBR_FRONTEND_ROOT}"
  KEEN_PBR_SYSTEM_RESOLVER_HOOK="${KEEN_PBR_SYSTEM_RESOLVER_HOOK}"
  KEEN_PBR_TARGET_OS="${KEEN_PBR_TARGET_OS}"
)
if(USE_KEENETIC_API)
  target_compile_definitions(keen-pbr PRIVATE USE_KEENETIC_API)
endif()
target_link_libraries(keen-pbr PRIVATE
  ${KEEN_PBR_CURL_TARGET}
  PkgConfig::LIBNL
  PkgConfig::LIBNL_ROUTE
  Threads::Threads
  cpptrace::cpptrace
  fmt::fmt-header-only
  keen_pbr_libunwind
  nlohmann_json::nlohmann_json
)
keen_pbr_apply_project_options(keen-pbr)

if(LIBATOMIC)
  target_link_libraries(keen-pbr PRIVATE ${LIBATOMIC})
endif()

target_link_libraries(keen-pbr PRIVATE ${LIBRESOLV})

if(WITH_API)
  target_compile_definitions(keen-pbr PRIVATE WITH_API)
  target_link_libraries(keen-pbr PRIVATE httplib::httplib)
  if(HTTPLIB_ANL_LIB)
    target_link_libraries(keen-pbr PRIVATE ${HTTPLIB_ANL_LIB})
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(keen-pbr PRIVATE stdc++fs)
endif()

install(TARGETS keen-pbr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(WITH_API)
  set(KEEN_PBR_PACKAGED_CONFIG "${CMAKE_SOURCE_DIR}/packages/common/config.full.example.json")
else()
  set(KEEN_PBR_PACKAGED_CONFIG "${CMAKE_SOURCE_DIR}/packages/common/config.headless.example.json")
endif()

install(FILES ${KEEN_PBR_PACKAGED_CONFIG}
  DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/keen-pbr
  RENAME config.json)
install(FILES ${CMAKE_SOURCE_DIR}/packages/common/local.lst
  DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/keen-pbr)

# Unit tests (optional, requires doctest submodule)
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
