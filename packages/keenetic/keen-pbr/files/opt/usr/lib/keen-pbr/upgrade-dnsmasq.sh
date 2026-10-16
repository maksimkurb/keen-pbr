#!/bin/sh
# keen-pbr 3.3.x depended on dnsmasq, which Entware provides as dnsmasq-full.
# 3.4 dropped the dependency, so on upgrade opkg removes dnsmasq-full as an
# orphaned auto-installed package, before any script of the new package runs.
# This script asks what to do about it.
#
#   upgrade-dnsmasq.sh postinst  run by postinst while opkg holds its lock:
#                                when the config comes from 3.3.x and
#                                dnsmasq-full is gone, asks on the terminal
#                                whether to keep dnsmasq or turn it off
#   upgrade-dnsmasq.sh install   waits for opkg to exit, installs dnsmasq-full
#                                and keeps dnsmasq management on (started in
#                                the background by `postinst`)
#
# A config with a "version" field was written by 3.4+ and never triggers it,
# and neither does an installed dnsmasq-full, however it was installed.

# shellcheck source=/dev/null
. "${KEEN_PBR_DEFAULTS:-/opt/etc/keen-pbr/defaults}"

DNSMASQ_PKG="dnsmasq-full"
OPKG_STATUS="${OPKG_STATUS:-/opt/lib/opkg/status}"
OPKG_LOCK="${OPKG_LOCK:-/opt/var/lock/opkg.lock}"
OPKG_WAIT_SECONDS="${OPKG_WAIT_SECONDS:-600}"
DNSMASQ_HOOK="${DNSMASQ_HOOK:-/opt/usr/lib/keen-pbr/dnsmasq-hook.sh}"
SERVICE="${SERVICE:-/opt/etc/init.d/S80keen-pbr}"
INSTALL_LOG="${INSTALL_LOG:-/tmp/keen-pbr-dnsmasq-install.log}"

log() {
    logger -t keen-pbr "upgrade-dnsmasq: $1" >/dev/null 2>&1 || true
}

dnsmasq_installed() {
    awk -v pkg="$DNSMASQ_PKG" '
        $1 == "Package:" { cur = $2 }
        $1 == "Status:" && cur == pkg && $NF == "installed" { found = 1 }
        END { exit found ? 0 : 1 }
    ' "$OPKG_STATUS" 2>/dev/null
}

decision_needed() {
    [ -f "$CONFIG" ] || return 1
    grep -q '"version"[[:space:]]*:' "$CONFIG" && return 1
    ! dnsmasq_installed
}

# In a subshell: a failed redirection of a special builtin would end the script.
have_tty() {
    (: </dev/tty >/dev/tty) 2>/dev/null
}

finish_upgrade() {
    "$KEEN_PBR" --config "$CONFIG" dnsmasq finish-upgrade "$1"
}

do_postinst() {
    decision_needed || return 0

    choice=1
    if have_tty; then
        cat >/dev/tty <<EOF

!!! dnsmasq has been removed !!!
keen-pbr 3.3.x installed $DNSMASQ_PKG as its dependency. keen-pbr 3.4 does not
need it, so opkg has just removed it. Your config still uses dnsmasq for
DNS servers and DNS rules.

  1) Keep dnsmasq: install $DNSMASQ_PKG again (right after opkg finishes)
  2) Stop using dnsmasq: turn dnsmasq management off in keen-pbr

EOF
        while :; do
            printf 'Choose 1 or 2: ' >/dev/tty
            if ! read -r choice </dev/tty; then
                choice=1
                echo >/dev/tty
                break
            fi
            case "$choice" in
                1|2) break ;;
            esac
        done
    fi

    if [ "$choice" = 2 ]; then
        log "dnsmasq management turned off after upgrade"
        finish_upgrade off
        echo "If you enabled 'opkg dns-override' for dnsmasq, turn it off in the router CLI:"
        echo "  no opkg dns-override"
        echo "  system configuration save"
        return 0
    fi

    log "reinstalling $DNSMASQ_PKG after upgrade"
    : >"$INSTALL_LOG"
    "$0" install </dev/null >/dev/null 2>&1 &
    echo "$DNSMASQ_PKG will be installed as soon as opkg finishes."
    if have_tty; then
        echo "Its progress will be printed in this terminal; please wait for it."
    fi
    echo "Log: $INSTALL_LOG"
}

# Prints to the terminal of the upgrade (when there is one) and to the log.
say() {
    printf '%s\n' "$*" >>"$INSTALL_LOG"
    if have_tty; then printf '%s\n' "$*" >/dev/tty; fi
}

# Runs a command, showing its output on the terminal and in the log.
show_run() {
    if have_tty; then
        { "$@" 2>&1; echo "rc=$?" >"$INSTALL_LOG.rc"; } | tee -a "$INSTALL_LOG" >/dev/tty
    else
        { "$@" 2>&1; echo "rc=$?" >"$INSTALL_LOG.rc"; } >>"$INSTALL_LOG"
    fi
    rc="$(sed -n 's/^rc=//p' "$INSTALL_LOG.rc" 2>/dev/null)"
    rm -f "$INSTALL_LOG.rc"
    return "${rc:-1}"
}

do_install() {
    # Keep going when the SSH session that ran the upgrade closes.
    trap '' HUP
    waited=0
    while [ -e "$OPKG_LOCK" ] || pidof opkg >/dev/null 2>&1; do
        if [ "$waited" -ge "$OPKG_WAIT_SECONDS" ]; then
            say "keen-pbr: opkg is still running; install dnsmasq yourself:"
            say "  opkg install $DNSMASQ_PKG && $KEEN_PBR --config $CONFIG dnsmasq finish-upgrade keep && $SERVICE restart"
            log "gave up waiting for opkg"
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done

    say ""
    say "keen-pbr: installing $DNSMASQ_PKG..."
    if ! show_run opkg install "$DNSMASQ_PKG"; then
        say "keen-pbr: refreshing the package lists and trying again..."
        if ! show_run opkg update || ! show_run opkg install "$DNSMASQ_PKG"; then
            say "keen-pbr: could not install $DNSMASQ_PKG. Install it yourself, then run:"
            say "  $KEEN_PBR --config $CONFIG dnsmasq finish-upgrade keep && $SERVICE restart"
            log "$DNSMASQ_PKG install failed, see $INSTALL_LOG"
            return 1
        fi
    fi

    show_run "$DNSMASQ_HOOK" apply entware:default ||
        say "keen-pbr: could not add keen-pbr to /opt/etc/dnsmasq.conf"
    show_run finish_upgrade keep
    show_run "$SERVICE" restart
    say "keen-pbr: $DNSMASQ_PKG is installed, dnsmasq management stays on."
    log "$DNSMASQ_PKG reinstalled"
}

case "${1:-}" in
    postinst) do_postinst ;;
    install) do_install ;;
    *)
        echo "usage: $0 postinst | install" >&2
        exit 64
        ;;
esac
