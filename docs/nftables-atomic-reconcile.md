# nftables atomic reconciliation

Normal nftables applies are submitted as one `nft -j -f -` JSON transaction.
keen-pbr never deletes `inet KeenPbrTable` before an ordinary replacement, so a
rejected transaction leaves the previous table unchanged. Existing static sets
are flushed and refilled in that same transaction; incompatible set schemas are
deleted and recreated alongside the replacement chain and rules. Dynamic
`kpbr4d_` and `kpbr6d_` dnsmasq sets are preserved by routine applies.

Port-only rules are emitted separately for IPv4 and IPv6. When IPv6 is disabled
in configuration, no IPv6 set or rule is generated; IPv6 is intentionally
unmanaged by this backend configuration.

