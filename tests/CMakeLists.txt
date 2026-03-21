add_executable(keen-pbr-tests
  test_main.cpp
  test_firewall_verifier.cpp
  test_nftables_builder.cpp
  test_iptables_builder.cpp
  test_cron.cpp
  test_addr_spec.cpp
  test_dnsmasq_gen.cpp
  test_dns_server.cpp
  test_dns_probe_server.cpp
  test_list_set_usage.cpp
  test_config_validation.cpp
  test_routing_state.cpp
  ../src/config/config.cpp
  ../src/config/routing_state.cpp
  ../src/firewall/iptables_verifier.cpp
  ../src/firewall/nftables_verifier.cpp
  ../src/firewall/firewall_verifier.cpp
  ../src/firewall/nftables.cpp
  ../src/firewall/nft_batch_pipe.cpp
  ../src/firewall/iptables.cpp
  ../src/firewall/ipset_restore_pipe.cpp
  ../src/log/logger.cpp
  ../src/dns/dnsmasq_gen.cpp
  ../src/dns/dns_router.cpp
  ../src/dns/dns_server.cpp
  ../src/dns/dns_probe_server.cpp
  ../src/cache/cache_manager.cpp
  ../src/lists/list_streamer.cpp
  ../src/lists/list_set_usage.cpp
  ../src/config/list_parser.cpp
  ../src/http/http_client.cpp
  ../src/routing/netlink.cpp
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

target_include_directories(keen-pbr-tests PRIVATE
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

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(keen-pbr-tests PRIVATE stdc++fs)
endif()
