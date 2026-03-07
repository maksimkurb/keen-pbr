add_executable(keen-pbr3-tests
  test_firewall_verifier.cpp
  test_nftables_builder.cpp
  test_iptables_builder.cpp
  test_cron.cpp
  ../src/config/config.cpp
  ../src/firewall/iptables_verifier.cpp
  ../src/firewall/nftables_verifier.cpp
  ../src/firewall/firewall_verifier.cpp
  ../src/firewall/nftables.cpp
  ../src/firewall/nft_batch_pipe.cpp
  ../src/firewall/iptables.cpp
  ../src/firewall/ipset_restore_pipe.cpp
  ../src/log/logger.cpp
)

target_compile_definitions(keen-pbr3-tests PRIVATE KEEN_PBR3_TESTING)

target_include_directories(keen-pbr3-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/doctest
  ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include
  ${LIBNL_INCLUDE_DIRS}
  ${LIBNL_ROUTE_INCLUDE_DIRS}
)

target_link_libraries(keen-pbr3-tests PRIVATE
  nlohmann_json::nlohmann_json
)

target_compile_features(keen-pbr3-tests PRIVATE cxx_std_20)
