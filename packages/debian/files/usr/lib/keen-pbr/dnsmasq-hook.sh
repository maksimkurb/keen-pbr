#!/bin/sh
# keen-pbr dnsmasq hook (Debian): dnsmasq-hook.sh apply|remove|status
#
# Installs /etc/dnsmasq.d/keen-pbr-upstream-dns.conf (written only when its
# content differs) and restarts dnsmasq.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/usr/sbin/keen-pbr}"
DNSMASQ_CONF_DIR="${DNSMASQ_CONF_DIR:-/etc/dnsmasq.d}"
RESTART_CMD="${RESTART_CMD:-systemctl restart dnsmasq}"
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
    *)
        echo "usage: $0 apply|remove|status" >&2
        exit 64
        ;;
esac
exit 0
