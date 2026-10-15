#!/bin/sh
# keen-pbr dnsmasq hook (Debian):
#   dnsmasq-hook.sh inspect | apply <selector> | remove <selector> | uninstall |
#                   install-recommended-config | status | alive
#
# The drop-in /etc/dnsmasq.d/keen-pbr.conf holds one conf-script line and stays
# in place.  On every dnsmasq start dnsmasq.sh runs `keen-pbr
# generate-resolver-config dnsmasq`, which reads config.json and prints either
# the generated config or the fallback servers.  So switching the integration
# on or off never edits dnsmasq's config; it only restarts dnsmasq.
#
#   apply <selector>   creates the drop-in when it is missing (once), restarts
#                      dnsmasq.  Extra arguments are ignored.
#   remove <selector>  integration switched off: changes no file, restarts dnsmasq.
#   uninstall          package removal (never runs keen-pbr): deletes the drop-in;
#                      when dnsmasq.conf has no-resolv and no upstream of its
#                      own, leaves the fallback servers in it instead.
#   install-recommended-config
#                      replaces /etc/dnsmasq.conf with dnsmasq.recommended.conf
#                      after `dnsmasq --test` accepts it (the old file is kept
#                      as dnsmasq.conf.backup-keenpbr) and creates the drop-in.
#
# `alive` exits 0 when dnsmasq is active, 1 when it is not and 2 when that
# cannot be told (no systemctl).

DNSMASQ_SH="${DNSMASQ_SH:-/usr/lib/keen-pbr/dnsmasq.sh}"
DNSMASQ_CONF_DIR="${DNSMASQ_CONF_DIR:-/etc/dnsmasq.d}"
RESTART_CMD="${RESTART_CMD:-systemctl restart dnsmasq}"
# Overridable for tests; unset means `systemctl is-active --quiet dnsmasq`.
ALIVE_CMD="${ALIVE_CMD:-}"
CONF_FILE="$DNSMASQ_CONF_DIR/keen-pbr.conf"
UNIT="${UNIT:-dnsmasq.service}"
SELECTOR="systemd:$UNIT"
SYSTEMCTL="${SYSTEMCTL:-systemctl}"
DEFAULT_CONF="${DEFAULT_CONF:-/etc/dnsmasq.conf}"
RECOMMENDED_CONF="${RECOMMENDED_CONF:-/usr/lib/keen-pbr/dnsmasq.recommended.conf}"
BACKUP_CONF="${BACKUP_CONF:-$DEFAULT_CONF.backup-keenpbr}"
FALLBACK_CONF="${FALLBACK_CONF:-/etc/keen-pbr/dnsmasq-fallback.conf}"
DNSMASQ_BIN="${DNSMASQ_BIN:-}"
LOCK_DIR="${LOCK_DIR:-/tmp/keen-pbr-dnsmasq-hook.lock}"
LOCK_TRIES="${LOCK_TRIES:-30}"
# `uninstall` gives up waiting sooner and then goes on without the lock.
UNINSTALL_LOCK_TRIES="${UNINSTALL_LOCK_TRIES:-5}"
FALLBACK_BEGIN_MARK="# keen-pbr fallback upstream (added on removal; replace or remove)"
# Used by `uninstall` when dnsmasq-fallback.conf is missing or has no servers.
DEFAULT_FALLBACK_SERVERS="server=8.8.8.8
server=8.8.4.4"

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

# take_lock <tries>: returns 1 when a live owner still holds the lock.
take_lock() {
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
        [ "$_tries" -ge "$1" ] && return 1
        sleep 1
    done
    echo "$$" > "$LOCK_DIR/pid"
    trap release_lock EXIT
    trap 'release_lock; exit 130' INT TERM
}

acquire_lock() {
    take_lock "$LOCK_TRIES" || die 4 "another dnsmasq-hook operation is running"
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

# dropin_state <file> <expected-line>: prints absent|current|leftover|foreign.
# "leftover" is the file `uninstall` left behind (marked fallback servers): a
# reinstall may take it over.
dropin_state() {
    if [ ! -e "$1" ]; then echo absent; return 0; fi
    if [ "$(cat "$1" 2>/dev/null)" = "$2" ]; then echo current; return 0; fi
    if [ "$(head -n 1 "$1" 2>/dev/null)" = "$FALLBACK_BEGIN_MARK" ]; then echo leftover; return 0; fi
    echo foreign
}

usage() {
    echo "usage: $0 inspect | apply <selector> | remove <selector> | uninstall | install-recommended-config | status | alive [<selector>]" >&2
    exit 64
}

check_selector() {
    [ -n "$1" ] || usage
    [ "$1" = "$SELECTOR" ] || die 3 "unknown dnsmasq selector: $1 (only $SELECTOR exists here)"
}

conf_line() { printf 'conf-script=%s dnsmasq-config-entry' "$DNSMASQ_SH"; }

dnsmasq_binary() {
    printf '%s' "${DNSMASQ_BIN:-$(command -v dnsmasq 2>/dev/null || true)}"
}

do_inspect() {
    printf 'hook_version=2\nplatform=debian\n'
    _bin="$(dnsmasq_binary)"
    printf 'binary=%s\n' "$_bin"
    [ -n "$_bin" ] || exit 2
    _en=1
    "$SYSTEMCTL" is-enabled --quiet "$UNIT" >/dev/null 2>&1 || _en=0
    _pid="$("$SYSTEMCTL" show -p MainPID --value "$UNIT" 2>/dev/null || echo 0)"
    case "$_pid" in ''|*[!0-9]*) _pid=0 ;; esac
    _st="$(dropin_state "$CONF_FILE" "$(conf_line)")"
    [ "$_st" = leftover ] && _st=absent
    printf '[instance]\n'
    printf 'selector=%s\n' "$SELECTOR"
    printf 'enabled=%s\n' "$_en"
    printf 'main_pid=%s\n' "$_pid"
    printf 'default_conf_file=%s\n' "$DEFAULT_CONF"
    printf 'dropin_include=static\n'
    printf 'dropin_dir=%s\n' "$DNSMASQ_CONF_DIR"
    printf 'dropin_file=%s\n' "$CONF_FILE"
    printf 'dropin_content=%s\n' "$(conf_line)"
    printf 'dropin_state=%s\n' "$_st"
}

do_restart() {
    eval "$RESTART_CMD" >/dev/null 2>&1 || die 1 "failed to restart dnsmasq"
}

# Creates the drop-in when it is missing; nothing is written when it is there.
ensure_dropin() {
    case "$(dropin_state "$CONF_FILE" "$(conf_line)")" in
        foreign) die 1 "$CONF_FILE exists and was not written by keen-pbr; not overwriting it" ;;
    esac
    write_line_atomic "$CONF_FILE" "$(conf_line)" || die 1 "cannot write $CONF_FILE"
}

do_apply() {
    check_selector "$1"
    if [ -z "$(dnsmasq_binary)" ]; then
        die 2 "dnsmasq is not installed (apt install dnsmasq)"
    fi
    [ -d "$DNSMASQ_CONF_DIR" ] || die 2 "dnsmasq is not installed ($DNSMASQ_CONF_DIR is missing)"
    acquire_lock
    ensure_dropin
    do_restart
}

do_remove() {
    check_selector "$1"
    acquire_lock
    do_restart
}

# Text that decides whether dnsmasq has an upstream of its own: dnsmasq.conf
# and the other drop-ins (not ours).
other_config_text() {
    cat "$DEFAULT_CONF" 2>/dev/null || true
    for _f in "$DNSMASQ_CONF_DIR"/*.conf; do
        [ -f "$_f" ] || continue
        [ "$_f" = "$CONF_FILE" ] && continue
        cat "$_f" 2>/dev/null || true
    done
}

# no-resolv is set and nothing gives dnsmasq an upstream: no unscoped server=
# and no servers-file=.  Per-domain server=/x/y lines are not an upstream.
needs_fallback_upstream() {
    printf '%s\n' "$1" | grep -q '^[[:space:]]*no-resolv' || return 1
    printf '%s\n' "$1" | grep -q '^[[:space:]]*\(server=[^/]\|servers-file=\)' && return 1
    return 0
}

# Package removal.  Never runs keen-pbr (it may be broken or already gone),
# does not stop at the first failure and does not wait forever for the lock:
# the daemon is stopped by then, so a held lock is only a stuck hook.
do_uninstall() {
    take_lock "$UNINSTALL_LOCK_TRIES" || log "uninstall: the lock is held; continuing without it"
    _status=0
    [ -e "$CONF_FILE" ] || return 0
    case "$(dropin_state "$CONF_FILE" "$(conf_line)")" in
        current) ;;
        *) log "uninstall: $CONF_FILE was changed; leaving it"; return 0 ;;
    esac
    if needs_fallback_upstream "$(other_config_text)"; then
        _servers="$(grep '^[[:space:]]*server=' "$FALLBACK_CONF" 2>/dev/null || true)"
        [ -n "$_servers" ] || _servers="$DEFAULT_FALLBACK_SERVERS"
        _body="$(printf '%s\n%s' "$FALLBACK_BEGIN_MARK" "$_servers")"
        if write_line_atomic "$CONF_FILE" "$_body"; then
            log "dnsmasq has no upstream of its own; left the fallback servers in $CONF_FILE"
        else
            _status=1
        fi
    elif rm -f "$CONF_FILE"; then
        log "removed $CONF_FILE"
    else
        _status=1
    fi
    if [ -n "$(dnsmasq_binary)" ]; then
        eval "$RESTART_CMD" >/dev/null 2>&1 || { log "uninstall: failed to restart dnsmasq"; _status=1; }
    fi
    return "$_status"
}

# Replaces /etc/dnsmasq.conf with the recommended one.  Nothing changes unless
# `dnsmasq --test` accepts the new file.
do_install_recommended() {
    _bin="$(dnsmasq_binary)"
    [ -n "$_bin" ] || die 2 "dnsmasq is not installed (apt install dnsmasq)"
    [ -f "$RECOMMENDED_CONF" ] || die 1 "$RECOMMENDED_CONF is missing"
    acquire_lock
    if [ -f "$DEFAULT_CONF" ] && cmp -s "$RECOMMENDED_CONF" "$DEFAULT_CONF"; then
        if [ "$(dropin_state "$CONF_FILE" "$(conf_line)")" = current ]; then
            echo "$DEFAULT_CONF already has the recommended settings; nothing changed."
            return 0
        fi
        ensure_dropin
        do_restart
        echo "$DEFAULT_CONF already has the recommended settings. Created $CONF_FILE."
        return 0
    fi
    _candidate="$DEFAULT_CONF.keen-pbr-new.$$"
    cp "$RECOMMENDED_CONF" "$_candidate" || { rm -f "$_candidate"; die 1 "cannot write $_candidate"; }
    if ! _out="$("$_bin" --test --conf-file="$_candidate" 2>&1)"; then
        rm -f "$_candidate"
        printf '%s\n' "$_out" >&2
        die 1 "dnsmasq rejected the recommended settings; $DEFAULT_CONF was not changed"
    fi
    if [ -f "$DEFAULT_CONF" ]; then
        cp "$DEFAULT_CONF" "$BACKUP_CONF" || { rm -f "$_candidate"; die 1 "cannot write $BACKUP_CONF"; }
    fi
    mv -f "$_candidate" "$DEFAULT_CONF" || { rm -f "$_candidate"; die 1 "cannot replace $DEFAULT_CONF"; }
    log "installed the recommended dnsmasq settings"
    ensure_dropin
    do_restart
    if [ -f "$BACKUP_CONF" ]; then
        echo "Replaced $DEFAULT_CONF with the recommended settings. The previous file is saved as $BACKUP_CONF."
    else
        echo "Wrote the recommended settings to $DEFAULT_CONF."
    fi
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
    apply) do_apply "${2:-}" ;;
    remove) do_remove "${2:-}" ;;
    uninstall)
        do_uninstall
        exit $?
        ;;
    install-recommended-config) do_install_recommended ;;
    status)
        if [ "$(dropin_state "$CONF_FILE" "$(conf_line)")" = current ]; then echo installed; else echo not-installed; fi
        ;;
    alive)
        do_alive
        exit $?
        ;;
    *) usage ;;
esac
exit 0
