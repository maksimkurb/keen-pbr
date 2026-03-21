#!/opt/bin/sh

[ "$table" != "mangle" -a "$table" != "nat" ] && exit 0

. /opt/etc/keen-pbr/defaults

PID="$(pidof "$(basename "$KEEN_PBR")")"
[ -n "$PID" ] || exit 0

logger -t "keen-pbr" "Refreshing routing state after netfilter change"
kill -SIGUSR1 "$PID"

if [ -f /opt/etc/keen-pbr/hook.sh ]; then
    keen_pbr_hook="netfilter"
    . /opt/etc/keen-pbr/hook.sh
fi

exit 0
