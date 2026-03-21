#!/opt/bin/sh

[ "$1" = "hook" ] || exit 0
[ "$layer" = "ctrl" ] || exit 0

logger -t "keen-pbr" "Refreshing ip routes and ip rules (iface='$system_name' layer='$layer', level='$level')"
/opt/etc/init.d/S80keen-pbr reapply-firewall >/dev/null 2>&1 || exit 0

if [ -f /opt/etc/keen-pbr/hook.sh ]; then
    keen_pbr_hook="iflayerchanged"
    . /opt/etc/keen-pbr/hook.sh
fi

exit 0
