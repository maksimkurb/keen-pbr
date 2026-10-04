#!/bin/sh
# keen-pbr dnsmasq hook (Keenetic/Entware): dnsmasq-hook.sh apply|remove|status|alive
#
# The conf-script drop-in lives in tmpfs (/tmp); the only persistent change is
# a managed block in /opt/etc/dnsmasq.conf that includes that directory, and it
# is written once (only when missing).
#
# `alive` exits 0 when the Entware dnsmasq runs, 1 when it definitely does not
# and 2 when that cannot be told.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/opt/usr/bin/keen-pbr}"
DNSMASQ_CONF="${DNSMASQ_CONF:-/opt/etc/dnsmasq.conf}"
INIT_SCRIPT="${INIT_SCRIPT:-/opt/etc/init.d/S56dnsmasq}"
RESTART_CMD="${RESTART_CMD:-$INIT_SCRIPT restart}"
ALIVE_CMD="${ALIVE_CMD:-$INIT_SCRIPT check}"
TMP_CONF_DIR="${TMP_CONF_DIR:-/tmp/keen-pbr/dnsmasq.d}"
CONF_NAME="keen-pbr-upstream-dns.conf"
BEGIN_MARK="# BEGIN keen-pbr upstream dns"
END_MARK="# END keen-pbr upstream dns"

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

block_present() {
    [ -f "$DNSMASQ_CONF" ] && grep -qxF "$BEGIN_MARK" "$DNSMASQ_CONF"
}

write_conf() {
    want="conf-script=$KEEN_PBR_BIN generate-resolver-config dnsmasq"
    f="$TMP_CONF_DIR/$CONF_NAME"
    if [ -f "$f" ] && [ "$(cat "$f" 2>/dev/null)" = "$want" ]; then
        return 0
    fi
    mkdir -p "$TMP_CONF_DIR" || return 1
    printf '%s\n' "$want" > "$f"
}

do_restart() {
    eval "$RESTART_CMD" >/dev/null 2>&1 || die 1 "failed to restart dnsmasq"
}

do_apply() {
    if [ ! -f "$INIT_SCRIPT" ] || [ ! -f "$DNSMASQ_CONF" ]; then
        die 2 "dnsmasq is not installed (opkg install dnsmasq-full)"
    fi
    write_conf || die 1 "cannot write $TMP_CONF_DIR/$CONF_NAME"
    if ! block_present; then
        {
            printf '\n%s\n' "$BEGIN_MARK"
            printf 'conf-dir=%s,*.conf\n' "$TMP_CONF_DIR"
            printf '%s\n' "$END_MARK"
        } >> "$DNSMASQ_CONF" || die 1 "cannot update $DNSMASQ_CONF"
        log "added managed block to $DNSMASQ_CONF"
    fi
    do_restart
    log "applied"
}

do_remove() {
    changed=0
    if [ -f "$TMP_CONF_DIR/$CONF_NAME" ]; then
        rm -f "$TMP_CONF_DIR/$CONF_NAME"
        changed=1
    fi
    if block_present; then
        tmp="$DNSMASQ_CONF.keen-pbr.tmp"
        awk -v b="$BEGIN_MARK" -v e="$END_MARK" '
            $0 == b { skip = 1; next }
            skip && $0 == e { skip = 0; next }
            !skip { print }
        ' "$DNSMASQ_CONF" > "$tmp" || { rm -f "$tmp"; die 1 "cannot update $DNSMASQ_CONF"; }
        cat "$tmp" > "$DNSMASQ_CONF"
        rm -f "$tmp"
        log "removed managed block from $DNSMASQ_CONF"
        changed=1
    fi
    [ "$changed" -eq 1 ] || return 0
    if [ -f "$INIT_SCRIPT" ]; then
        do_restart
    fi
    log "removed"
}

# Exit 0: the Entware dnsmasq runs, 1: it does not, 2: unknown.
# Entware's rc.func (sourced by S56dnsmasq) implements `check`, which prints
# "alive." or "dead." (exit status 0/1).  The init script is used instead of a
# bare `pidof dnsmasq` because the Keenetic firmware may run its own dnsmasq
# that must not count.  The printed word is trusted first, the exit status only
# when the output says neither.
do_alive() {
    [ -f "$INIT_SCRIPT" ] || return 2
    out="$(eval "$ALIVE_CMD" 2>/dev/null)"
    rc=$?
    case "$out" in
        *alive*) return 0 ;;
        *dead*) return 1 ;;
    esac
    case "$rc" in
        0) return 0 ;;
        1) return 1 ;;
        *) return 2 ;;
    esac
}

case "$1" in
    apply) do_apply ;;
    remove) do_remove ;;
    status)
        if block_present || [ -f "$TMP_CONF_DIR/$CONF_NAME" ]; then
            echo installed
        else
            echo not-installed
        fi
        ;;
    alive) do_alive ;;
    *)
        echo "usage: $0 apply|remove|status|alive" >&2
        exit 64
        ;;
esac
