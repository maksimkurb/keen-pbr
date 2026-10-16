#!/opt/bin/sh

[ "$type" = "iptables" ] || exit 0
# mangle holds the routing rules, nat the source NAT of DNS detours; NDM
# rebuilds each of them on its own.
case "$table" in
    mangle|nat) ;;
    *) exit 0 ;;
esac

logger -t "keen-pbr" "Refreshing routing state after netfilter change ($table)"
/opt/etc/init.d/S80keen-pbr reapply-firewall >/dev/null 2>&1 || exit 0

if [ "$table" = "mangle" ] && [ -f /opt/etc/keen-pbr/hook.sh ]; then
    keen_pbr_hook="netfilter"
    . /opt/etc/keen-pbr/hook.sh
fi

exit 0
