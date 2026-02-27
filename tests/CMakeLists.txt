add_executable(keen-pbr3-tests
  test_firewall_verifier.cpp
  ../src/firewall/iptables_verifier.cpp
  ../src/firewall/nftables_verifier.cpp
  ../src/firewall/firewall_verifier.cpp
)

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
