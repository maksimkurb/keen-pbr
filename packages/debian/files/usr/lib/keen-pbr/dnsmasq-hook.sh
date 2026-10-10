#!/bin/sh
# keen-pbr dnsmasq hook (Debian): dnsmasq-hook.sh apply|remove|status|alive
#
# Installs /etc/dnsmasq.d/keen-pbr-upstream-dns.conf (written only when its
# content differs) and restarts dnsmasq.
#
# `alive` exits 0 when dnsmasq is active, 1 when it is not and 2 when that
# cannot be told (no systemctl).

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/usr/sbin/keen-pbr}"
DNSMASQ_CONF_DIR="${DNSMASQ_CONF_DIR:-/etc/dnsmasq.d}"
RESTART_CMD="${RESTART_CMD:-systemctl restart dnsmasq}"
# Overridable for tests; unset means `systemctl is-active --quiet dnsmasq`.
ALIVE_CMD="${ALIVE_CMD:-}"
CONF_FILE="$DNSMASQ_CONF_DIR/keen-pbr-upstream-dns.conf"

log() {
    logger -t keen-pbr "dnsmasq-hook: $1" >/dev/null 2>&1 || true
}

die() {
    code="$1"
    shift
    printf '%s\n' "$*" >&2
    log "$*"
    exit "$code"
}

do_restart() {
    eval "$RESTART_CMD" >/dev/null 2>&1 || die 1 "failed to restart dnsmasq"
}

do_alive() {
    cmd="$ALIVE_CMD"
    if [ -z "$cmd" ]; then
        command -v systemctl >/dev/null 2>&1 || return 2
        cmd="systemctl is-active --quiet dnsmasq"
    fi
    eval "$cmd" >/dev/null 2>&1
    case "$?" in
        0) return 0 ;;
        1|3) return 1 ;;  # `systemctl is-active` exits 3 for inactive/failed
        *) return 2 ;;
    esac
}

case "$1" in
    apply)
        if ! command -v dnsmasq >/dev/null 2>&1; then
            die 2 "dnsmasq is not installed (apt install dnsmasq)"
        fi
        [ -d "$DNSMASQ_CONF_DIR" ] || die 2 "dnsmasq is not installed ($DNSMASQ_CONF_DIR is missing)"
        want="conf-script=$KEEN_PBR_BIN generate-resolver-config dnsmasq"
        if [ ! -f "$CONF_FILE" ] || [ "$(cat "$CONF_FILE" 2>/dev/null)" != "$want" ]; then
            printf '%s\n' "$want" > "$CONF_FILE" || die 1 "cannot write $CONF_FILE"
            log "wrote $CONF_FILE"
        fi
        do_restart
        ;;
    remove)
        [ -e "$CONF_FILE" ] || exit 0
        rm -f "$CONF_FILE" || die 1 "cannot remove $CONF_FILE"
        log "removed $CONF_FILE"
        do_restart
        ;;
    status)
        if [ -f "$CONF_FILE" ]; then echo installed; else echo not-installed; fi
        ;;
    alive)
        do_alive
        exit $?
        ;;
    *)
        echo "usage: $0 apply|remove|status|alive" >&2
        exit 64
        ;;
esac
exit 0
