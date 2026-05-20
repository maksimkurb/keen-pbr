#!/opt/bin/sh

[ "$table" != "mangle" -a "$table" != "nat" ] && exit 0

logger -t "keen-pbr" "Refreshing routing state after netfilter change"
/opt/etc/init.d/S80keen-pbr reapply-firewall >/dev/null 2>&1 || exit 0

# Re-assert HW NAT (fastnat) is disabled after every netfilter change.
# NDM re-enables it on network events, which silently breaks policy routing
# because fastnat makes marked packets bypass the mangle table.
# Hot-path cost: two sysctl reads; sysctl -w is skipped when already zero.
for _key in net.ipv4.netfilter.ip_conntrack_fastnat net.netfilter.nf_conntrack_fastnat; do
    _val="$(sysctl -n "$_key" 2>/dev/null)"
    [ -z "$_val" ] && continue
    if [ "$_val" != "0" ]; then
        sysctl -w "$_key=0" 2>/dev/null || true
        logger -t "keen-pbr" "Re-disabled HW NAT fastnat ($_key was $_val)"
    fi
done
unset _key _val

if [ -f /opt/etc/keen-pbr/hook.sh ]; then
    keen_pbr_hook="netfilter"
    . /opt/etc/keen-pbr/hook.sh
fi

exit 0
