#!/opt/bin/sh

[ "$1" = "hook" ] || exit 0
[ "$layer" = "ctrl" ] || exit 0

. /opt/etc/keen-pbr/defaults

PID="$(pidof "$(basename "$KEEN_PBR")")"
[ -n "$PID" ] || exit 0

logger -t "keen-pbr" "Refreshing ip routes and ip rules (iface='$system_name' layer='$layer', level='$level')"
kill -SIGUSR1 "$PID"

if [ -f /opt/etc/keen-pbr/hook.sh ]; then
    keen_pbr_hook="iflayerchanged"
    . /opt/etc/keen-pbr/hook.sh
fi

exit 0
