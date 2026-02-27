cmake_minimum_required(VERSION 3.14)
project(keen-pbr3 VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check for <format> header
include(CheckIncludeFileCXX)
check_include_file_cxx("format" HAS_FORMAT)
if(NOT HAS_FORMAT)
  message(FATAL_ERROR "C++20 <format> header not found. GCC 13+ or Clang 17+ required.")
endif()

# Size optimization
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

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
  src/config/list_parser.cpp
  src/lists/ipset.cpp
  src/lists/list_streamer.cpp
  src/cache/cache_manager.cpp
  src/routing/target.cpp
  src/routing/netlink.cpp
  src/routing/route_table.cpp
  src/routing/policy_rule.cpp
  src/health/circuit_breaker.cpp
  src/health/url_tester.cpp
  src/routing/urltest_manager.cpp
  src/routing/firewall_state.cpp
  src/firewall/firewall.cpp
  src/firewall/iptables.cpp
  src/firewall/nftables.cpp
  src/firewall/ipset_restore_pipe.cpp
  src/firewall/nft_batch_pipe.cpp
  src/firewall/firewall_verifier.cpp
  src/firewall/iptables_verifier.cpp
  src/dns/dns_server.cpp
  src/dns/dns_router.cpp
  src/dns/dnsmasq_gen.cpp
  src/daemon/daemon.cpp
  src/daemon/scheduler.cpp
  src/main.cpp
)

if(WITH_API)
  list(APPEND SOURCES src/api/server.cpp src/api/handlers.cpp)
endif()

# Executable
add_executable(keen-pbr3 ${SOURCES})
target_include_directories(keen-pbr3 PRIVATE include)
target_link_libraries(keen-pbr3 PRIVATE
  CURL::libcurl
  PkgConfig::LIBNL
  PkgConfig::LIBNL_ROUTE
  nlohmann_json::nlohmann_json
)

if(WITH_API)
  target_compile_definitions(keen-pbr3 PRIVATE WITH_API)
  target_link_libraries(keen-pbr3 PRIVATE httplib::httplib)
endif()

install(TARGETS keen-pbr3 DESTINATION bin)
