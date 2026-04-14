add_executable(keen-pbr-tests
  test_main.cpp
  test_firewall_verifier.cpp
  test_nftables_builder.cpp
  test_iptables_builder.cpp
  test_cron.cpp
  test_addr_spec.cpp
  test_dnsmasq_gen.cpp
  test_dns_txt_client.cpp
  test_dns_server.cpp
  test_keenetic_dns.cpp
  test_dns_probe_server.cpp
  test_list_set_usage.cpp
  test_list_service.cpp
  test_http_client.cpp
  test_config_validation.cpp
  test_routing_state.cpp
  test_runtime_interface_inventory.cpp
  test_api_runtime_interfaces.cpp
  test_resolver_health.cpp
  test_system_resolver_hook.cpp
  test_safe_exec.cpp
  test_trace_logging.cpp
  test_port_spec_util.cpp
  ../src/config/config.cpp
  ../src/config/routing_state.cpp
  ../src/firewall/iptables_verifier.cpp
  ../src/firewall/nftables_verifier.cpp
  ../src/firewall/firewall_verifier.cpp
  ../src/firewall/port_spec_util.cpp
  ../src/firewall/nftables.cpp
  ../src/firewall/nft_batch_pipe.cpp
  ../src/firewall/iptables.cpp
  ../src/firewall/ipset_restore_pipe.cpp
  ../src/log/logger.cpp
  ../src/log/trace.cpp
  ../src/dns/dnsmasq_gen.cpp
  ../src/dns/dns_txt_client.cpp
  ../src/dns/dns_router.cpp
  ../src/dns/dns_server.cpp
  ../src/dns/keenetic_dns.cpp
  ../src/daemon/system_resolver_hook.cpp
  ../src/dns/dns_probe_server.cpp
  ../src/cache/cache_manager.cpp
  ../src/lists/list_streamer.cpp
  ../src/lists/list_set_usage.cpp
  ../src/config/list_parser.cpp
  ../src/daemon/list_service.cpp
  ../src/daemon/resolver_health.cpp
  ../src/http/http_client.cpp
  ../src/routing/netlink.cpp
  ../src/util/blocking_executor.cpp
  ../src/util/traced_mutex.cpp
  ../src/routing/policy_rule.cpp
  ../src/routing/route_table.cpp
  ../src/routing/target.cpp
)
set_target_properties(keen-pbr-tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)

target_compile_definitions(keen-pbr-tests PRIVATE KEEN_PBR3_TESTING)

# Reuse include paths from the main target so generated headers such as
# <keen-pbr/version.hpp> are always visible to tests.
target_include_directories(keen-pbr-tests PRIVATE
  $<TARGET_PROPERTY:keen-pbr,INCLUDE_DIRECTORIES>
)

target_include_directories(keen-pbr-tests PRIVATE
  ${KEEN_PBR_GENERATED_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/doctest
  ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include
  ${LIBNL_INCLUDE_DIRS}
  ${LIBNL_ROUTE_INCLUDE_DIRS}
)

target_link_libraries(keen-pbr-tests PRIVATE
  nlohmann_json::nlohmann_json
  CURL::libcurl
  PkgConfig::LIBNL
  PkgConfig::LIBNL_ROUTE
  fmt::fmt-header-only
)

if(LIBRESOLV)
  target_link_libraries(keen-pbr-tests PRIVATE ${LIBRESOLV})
endif()

if(LIBATOMIC)
  target_link_libraries(keen-pbr-tests PRIVATE ${LIBATOMIC})
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(keen-pbr-tests PRIVATE stdc++fs)
endif()

if(USE_KEENETIC_API)
  target_compile_definitions(keen-pbr-tests PRIVATE USE_KEENETIC_API)
endif()

if(WITH_API)
  target_compile_definitions(keen-pbr-tests PRIVATE WITH_API)
  target_sources(keen-pbr-tests PRIVATE
    ../src/api/server.cpp
    ../src/api/sse_broadcaster.cpp
    ../src/api/handler_runtime_interfaces.cpp
    ../src/health/runtime_interface_inventory.cpp
  )
  target_link_libraries(keen-pbr-tests PRIVATE
    httplib::httplib
    Threads::Threads
  )
  if(HTTPLIB_ANL_LIB)
    target_link_libraries(keen-pbr-tests PRIVATE ${HTTPLIB_ANL_LIB})
  endif()
endif()
