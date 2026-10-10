#!/bin/sh
# keen-pbr dnsmasq hook (Keenetic/Entware): dnsmasq-hook.sh inspect|apply <selector>|remove <selector>|status|alive
#
# The conf-script drop-in lives in tmpfs (/tmp); the only persistent change is
# a managed block in /opt/etc/dnsmasq.conf that includes that directory, and it
# is written once (only when missing).  The conf-script is dnsmasq.sh, which
# prints keen-pbr's config while the daemon runs and the fallback upstreams
# otherwise.
#
# `alive` exits 0 when the Entware dnsmasq runs, 1 when it definitely does not
# and 2 when that cannot be told.

DNSMASQ_SH="${DNSMASQ_SH:-/opt/usr/lib/keen-pbr/dnsmasq.sh}"
DNSMASQ_CONF="${DNSMASQ_CONF:-/opt/etc/dnsmasq.conf}"
FALLBACK_CONF="${FALLBACK_CONF:-/opt/etc/keen-pbr/dnsmasq-fallback.conf}"
INIT_SCRIPT="${INIT_SCRIPT:-/opt/etc/init.d/S56dnsmasq}"
RESTART_CMD="${RESTART_CMD:-$INIT_SCRIPT restart}"
ALIVE_CMD="${ALIVE_CMD:-$INIT_SCRIPT check}"
TMP_CONF_DIR="${TMP_CONF_DIR:-/tmp/keen-pbr/dnsmasq.d}"
DNSMASQ_BIN="${DNSMASQ_BIN:-/opt/sbin/dnsmasq}"
SELECTOR="entware:default"
LOCK_DIR="${LOCK_DIR:-/tmp/keen-pbr-dnsmasq-hook.lock}"
LOCK_TRIES="${LOCK_TRIES:-30}"
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

conf_line() { printf 'conf-script=%s dnsmasq-config-entry' "$DNSMASQ_SH"; }
write_conf() { write_line_atomic "$TMP_CONF_DIR/$CONF_NAME" "$(conf_line)"; }

release_lock() { rm -rf "$LOCK_DIR"; }

acquire_lock() {
    _tries=0
    while ! mkdir "$LOCK_DIR" 2>/dev/null; do
        _owner="$(cat "$LOCK_DIR/pid" 2>/dev/null || true)"
        # A lock whose owner is gone is stale. An empty pid file means the
        # owner is between mkdir and writing its pid: wait, never steal.
        if [ -n "$_owner" ] && ! kill -0 "$_owner" 2>/dev/null; then
            rm -rf "$LOCK_DIR"
            continue
        fi
        _tries=$((_tries + 1))
        [ "$_tries" -ge "$LOCK_TRIES" ] && die 4 "another dnsmasq-hook operation is running"
        sleep 1
    done
    echo "$$" > "$LOCK_DIR/pid"
    trap release_lock EXIT
    trap 'release_lock; exit 130' INT TERM
}

# write_line_atomic <file> <line>: writes "<line>\n" only when the content differs.
# Returns 0 when unchanged or written, 1 on error.
write_line_atomic() {
    if [ -f "$1" ] && [ "$(cat "$1" 2>/dev/null)" = "$2" ]; then
        return 0
    fi
    mkdir -p "$(dirname "$1")" || return 1
    printf '%s\n' "$2" > "$1.kpbr.$$" || { rm -f "$1.kpbr.$$"; return 1; }
    mv -f "$1.kpbr.$$" "$1" || { rm -f "$1.kpbr.$$"; return 1; }
}

# dropin_state <file> <expected-line>: prints absent|current|foreign
dropin_state() {
    if [ ! -e "$1" ]; then echo absent; return 0; fi
    if [ "$(cat "$1" 2>/dev/null)" = "$2" ]; then echo current; return 0; fi
    echo foreign
}

usage() {
    echo "usage: $0 inspect | apply <selector> | remove <selector> | status | alive [<selector>]" >&2
    exit 64
}

check_selector() {
    [ -n "$1" ] || usage
    [ "$1" = "$SELECTOR" ] || die 3 "unknown dnsmasq selector: $1 (only $SELECTOR exists here)"
}

do_inspect() {
    printf 'hook_version=2\nplatform=keenetic\n'
    if [ ! -x "$DNSMASQ_BIN" ] || [ ! -f "$INIT_SCRIPT" ]; then
        printf 'binary=\n'
        exit 2
    fi
    printf 'binary=%s\n' "$DNSMASQ_BIN"
    _en=1
    grep -q '^ENABLED=no' "$INIT_SCRIPT" 2>/dev/null && _en=0
    _f="$TMP_CONF_DIR/$CONF_NAME"
    if block_present; then _st=current; else _st="$(dropin_state "$_f" "$(conf_line)")"; fi
    printf '[instance]\n'
    printf 'selector=%s\n' "$SELECTOR"
    printf 'enabled=%s\n' "$_en"
    printf 'exe_required=%s\n' "$DNSMASQ_BIN"
    printf 'match_conf_file=%s\n' "$DNSMASQ_CONF"
    printf 'default_conf_file=%s\n' "$DNSMASQ_CONF"
    printf 'dropin_include=managed\n'
    printf 'dropin_dir=%s\n' "$TMP_CONF_DIR"
    printf 'dropin_file=%s\n' "$_f"
    printf 'dropin_content=%s\n' "$(conf_line)"
    printf 'dropin_state=%s\n' "$_st"
}

# migrate-dnsmasq.sh may have written a marked fallback upstream block (when
# dnsmasq had no other upstream).  The conf-script supplies the fallback servers
# while keen-pbr is down, so the permanent block would only bypass the per-list
# upstreams: drop it when it is exactly what the migration wrote.
drop_fallback_block() {
    [ -f "$FALLBACK_CONF" ] || return 0
    tmp="$DNSMASQ_CONF.keen-pbr.tmp"
    rc=0
    awk -v begin="# BEGIN keen-pbr fallback upstream (added on upgrade; replace or remove)" \
        -v end="# END keen-pbr fallback upstream" '
        NR == FNR { if ($0 ~ /^[ \t]*server=/) allowed[$0] = 1; next }
        $0 == begin {
            if (inside) printf "%s", buffered
            inside = 1; keep = 0; buffered = $0 ORS; next
        }
        inside {
            buffered = buffered $0 ORS
            if ($0 ~ /^# BEGIN /) keep = 1
            else if ($0 != end && !($0 in allowed)) keep = 1
            if ($0 == end) {
                if (keep) printf "%s", buffered
                else removed = 1
                inside = 0; buffered = ""
            }
            next
        }
        { print }
        END { if (inside) printf "%s", buffered; exit(removed ? 10 : 0) }
    ' "$FALLBACK_CONF" "$DNSMASQ_CONF" > "$tmp" || rc=$?
    if [ "$rc" = 10 ]; then
        cat "$tmp" > "$DNSMASQ_CONF"
        log "removed the fallback upstream block from $DNSMASQ_CONF"
    fi
    rm -f "$tmp"
    return 0
}

do_restart() {
    eval "$RESTART_CMD" >/dev/null 2>&1 || die 1 "failed to restart dnsmasq"
}

do_apply() {
    check_selector "$1"
    if [ ! -f "$INIT_SCRIPT" ] || [ ! -f "$DNSMASQ_CONF" ]; then
        die 2 "dnsmasq is not installed (opkg install dnsmasq-full)"
    fi
    acquire_lock
    if [ "$(dropin_state "$TMP_CONF_DIR/$CONF_NAME" "$(conf_line)")" = foreign ]; then
        die 1 "$TMP_CONF_DIR/$CONF_NAME exists and was not written by keen-pbr"
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
    drop_fallback_block
    do_restart
    log "applied"
}

do_remove() {
    check_selector "$1"
    acquire_lock
    changed=0
    if [ "$(dropin_state "$TMP_CONF_DIR/$CONF_NAME" "$(conf_line)")" = current ]; then
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

case "${1:-}" in
    inspect) do_inspect ;;
    apply) do_apply "${2:-}" ;;
    remove) do_remove "${2:-}" ;;
    status)
        if block_present || [ -f "$TMP_CONF_DIR/$CONF_NAME" ]; then
            echo installed
        else
            echo not-installed
        fi
        ;;
    alive) do_alive ;;
    *) usage ;;
esac
