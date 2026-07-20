# iptables/ipset atomic reconciliation

The iptables backend uses `iptables-restore --noflush` with a fully built
generation (shadow) chain.  The stable `KeenPbrTable` PREROUTING and
`KeenPbrTable_OUTPUT` dispatchers are switched in one restore transaction, so a
failed transaction leaves the preceding generation reachable.

Static `kpbr4_`/`kpbr6_` sets are loaded into a temporary set and published with
`ipset swap`; the canonical name therefore always refers to either the previous
set or a complete replacement.  Dynamic `kpbr4d_`/`kpbr6d_` dnsmasq sets are
not recreated during `PreserveSets` applies, preserving learned entries and
their timeout metadata.

This shadow-chain path is the recorded capability baseline for the supported
iptables-restore versions. It deliberately avoids relying on an implementation-
specific in-place chain rewrite.

