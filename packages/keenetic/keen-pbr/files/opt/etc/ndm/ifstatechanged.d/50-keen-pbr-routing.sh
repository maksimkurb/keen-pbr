#!/opt/bin/sh

[ "$1" = "hook" ] || exit 0

STATE="$link-$connected-$up-$change"

is_terminal_state() {
    case "$STATE" in
        up-yes-up-connected|down-no-down-config|down-no-up-config)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

if is_terminal_state; then
    logger -t "keen-pbr" "Refreshing ip routes and ip rules (iface='$system_name', state='$STATE')"
    /opt/etc/init.d/S80keen-pbr reapply-firewall >/dev/null 2>&1 || exit 0

    if [ -f /opt/etc/keen-pbr/hook.sh ]; then
        keen_pbr_hook="ifstatechanged"
        . /opt/etc/keen-pbr/hook.sh
    fi
fi

exit 0
