Real kernel dumps used by `tests/test_firewall_physical.cpp`.

Capture (rootless, no sudo; iptables 1.8.13 nf_tables frontend, nftables 1.1.7):

    bash tests/firewall_it/scripts/capture_physical_fixtures.sh cmake-build-gcc/tests/keen-pbr-tests

The unit-test binary applies a plan through the real iptables/nftables backend
inside `unshare --user --map-root-user --net`, and the script dumps the result
with `iptables-save`, `iptables -S`, `nft -j list table inet KeenPbrTable` and
`nft list table inet KeenPbrTable`.  `*_foreign*` and `*_misc*` fixtures add
hand-made rules with the stock tools to show how the kernel prints them back.
Because the nft-backed iptables has no `/proc/net/ip_tables_matches`, the
capture test forces the comment capability on, as it is on legacy iptables.
