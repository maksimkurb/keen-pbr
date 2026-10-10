#!/bin/sh
# keen-pbr dnsmasq hook (Debian): dnsmasq-hook.sh inspect|apply <selector>|remove <selector>|status|alive
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
UNIT="${UNIT:-dnsmasq.service}"
SELECTOR="systemd:$UNIT"
SYSTEMCTL="${SYSTEMCTL:-systemctl}"
DEFAULT_CONF="${DEFAULT_CONF:-/etc/dnsmasq.conf}"
DNSMASQ_BIN="${DNSMASQ_BIN:-}"
LOCK_DIR="${LOCK_DIR:-/tmp/keen-pbr-dnsmasq-hook.lock}"
LOCK_TRIES="${LOCK_TRIES:-30}"

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

conf_line() { printf 'conf-script=%s generate-resolver-config dnsmasq' "$KEEN_PBR_BIN"; }

do_inspect() {
    printf 'hook_version=2\nplatform=debian\n'
    _bin="${DNSMASQ_BIN:-$(command -v dnsmasq 2>/dev/null || true)}"
    printf 'binary=%s\n' "$_bin"
    [ -n "$_bin" ] || exit 2
    _en=1
    "$SYSTEMCTL" is-enabled --quiet "$UNIT" >/dev/null 2>&1 || _en=0
    _pid="$("$SYSTEMCTL" show -p MainPID --value "$UNIT" 2>/dev/null || echo 0)"
    case "$_pid" in ''|*[!0-9]*) _pid=0 ;; esac
    printf '[instance]\n'
    printf 'selector=%s\n' "$SELECTOR"
    printf 'enabled=%s\n' "$_en"
    printf 'main_pid=%s\n' "$_pid"
    printf 'default_conf_file=%s\n' "$DEFAULT_CONF"
    printf 'dropin_include=static\n'
    printf 'dropin_dir=%s\n' "$DNSMASQ_CONF_DIR"
    printf 'dropin_file=%s\n' "$CONF_FILE"
    printf 'dropin_content=%s\n' "$(conf_line)"
    printf 'dropin_state=%s\n' "$(dropin_state "$CONF_FILE" "$(conf_line)")"
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

case "${1:-}" in
    inspect) do_inspect ;;
    apply)
        check_selector "${2:-}"
        if ! command -v dnsmasq >/dev/null 2>&1; then
            die 2 "dnsmasq is not installed (apt install dnsmasq)"
        fi
        [ -d "$DNSMASQ_CONF_DIR" ] || die 2 "dnsmasq is not installed ($DNSMASQ_CONF_DIR is missing)"
        acquire_lock
        if [ "$(dropin_state "$CONF_FILE" "$(conf_line)")" = foreign ]; then
            die 1 "$CONF_FILE exists and was not written by keen-pbr; not overwriting it"
        fi
        write_line_atomic "$CONF_FILE" "$(conf_line)" || die 1 "cannot write $CONF_FILE"
        do_restart
        ;;
    remove)
        check_selector "${2:-}"
        acquire_lock
        if [ "$(dropin_state "$CONF_FILE" "$(conf_line)")" = current ]; then
            rm -f "$CONF_FILE" || die 1 "cannot remove $CONF_FILE"
            log "removed $CONF_FILE"
            do_restart
        fi
        ;;
    status)
        if [ -f "$CONF_FILE" ]; then echo installed; else echo not-installed; fi
        ;;
    alive)
        do_alive
        exit $?
        ;;
    *) usage ;;
esac
exit 0
