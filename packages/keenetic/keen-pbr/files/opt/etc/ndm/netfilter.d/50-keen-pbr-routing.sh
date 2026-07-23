#!/opt/bin/sh

[ "$type" = "iptables" ] || exit 0
[ "$table" = "mangle" ] || exit 0

logger -t "keen-pbr" "Refreshing routing state after netfilter change"
/opt/etc/init.d/S80keen-pbr reapply-firewall >/dev/null 2>&1 || exit 0

if [ -f /opt/etc/keen-pbr/hook.sh ]; then
    keen_pbr_hook="netfilter"
    . /opt/etc/keen-pbr/hook.sh
fi

exit 0
