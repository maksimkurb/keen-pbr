# Keenetic iptables v1.4.21 fixtures

Captured on a real Keenetic router (iptables v1.4.21 legacy, ipset v7.24 with
kernel ipset protocol 6) running an older keen-pbr in raw-PREROUTING mode with
`inbound_interfaces = [br0]`, via:

    iptables -t mangle -S; iptables -t raw -S; ip6tables -t mangle -S; ip6tables -t raw -S

Lines are verbatim 1.4.21 output. The files are a trimmed subset (repetitive
set rules removed) and sanitized: device MAC addresses are replaced by
`02:00:00:00:00:0N` and the router's cloud SNI by `*example.keenetic.io`.

Notable facts captured here:
- No `-m comment`: this firmware has no xt_comment, so rules are keyless.
- Keenetic NDM proprietary matches/targets in system chains (`connndmmark`,
  `NDMMARK`, `CONNNDMMARK`, `connskip`, `tls`, `policy`) next to keen-pbr hooks.
- Both A and B generation chains populated at once (stale inactive slot).
- `! -i br0 -j RETURN` inside the OUTPUT chain (the inbound-filter bug that
  disables classification of router-originated traffic).
