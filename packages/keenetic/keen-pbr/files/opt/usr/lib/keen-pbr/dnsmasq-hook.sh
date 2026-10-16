#!/bin/sh
# keen-pbr dnsmasq hook (Keenetic/Entware):
#   dnsmasq-hook.sh inspect | apply <selector> | remove <selector> | uninstall |
#                   install-recommended-config | status | alive
#
# The conf-script entry lives in /opt/etc/dnsmasq.conf, in a managed block, and
# stays there.  On every dnsmasq start dnsmasq.sh runs `keen-pbr
# generate-resolver-config dnsmasq`, which reads config.json and prints either
# the generated config or the fallback servers.  So switching the integration
# on or off never edits dnsmasq's config; it only restarts dnsmasq.
#
#   apply <selector>   appends the managed block when it is missing (once),
#                      restarts dnsmasq.  Extra arguments are ignored.
#   remove <selector>  integration switched off: changes no file, restarts dnsmasq.
#   uninstall          package removal (never runs keen-pbr): removes the block;
#                      when dnsmasq.conf then has no-resolv and no upstream of
#                      its own, leaves the fallback servers in a marked block.
#   install-recommended-config
#                      replaces dnsmasq.conf with dnsmasq.recommended.conf after
#                      `dnsmasq --test` accepts it; the old file is kept as
#                      dnsmasq.conf.backup-keenpbr.
#
# `alive` exits 0 when the Entware dnsmasq runs, 1 when it definitely does not
# and 2 when that cannot be told.

DNSMASQ_SH="${DNSMASQ_SH:-/opt/usr/lib/keen-pbr/dnsmasq.sh}"
DNSMASQ_CONF="${DNSMASQ_CONF:-/opt/etc/dnsmasq.conf}"
RECOMMENDED_CONF="${RECOMMENDED_CONF:-/opt/usr/lib/keen-pbr/dnsmasq.recommended.conf}"
BACKUP_CONF="${BACKUP_CONF:-$DNSMASQ_CONF.backup-keenpbr}"
FALLBACK_CONF="${FALLBACK_CONF:-/opt/etc/keen-pbr/dnsmasq-fallback.conf}"
INIT_SCRIPT="${INIT_SCRIPT:-/opt/etc/init.d/S56dnsmasq}"
RESTART_CMD="${RESTART_CMD:-$INIT_SCRIPT restart}"
ALIVE_CMD="${ALIVE_CMD:-$INIT_SCRIPT check}"
DNSMASQ_BIN="${DNSMASQ_BIN:-/opt/sbin/dnsmasq}"
SELECTOR="entware:default"
LOCK_DIR="${LOCK_DIR:-/tmp/keen-pbr-dnsmasq-hook.lock}"
LOCK_TRIES="${LOCK_TRIES:-30}"
# `uninstall` gives up waiting sooner and then goes on without the lock.
UNINSTALL_LOCK_TRIES="${UNINSTALL_LOCK_TRIES:-5}"
BEGIN_MARK="# BEGIN keen-pbr managed block"
NOTICE_LINE="# Do not remove the following line, it is required for keen-pbr to function properly."
END_MARK="# END keen-pbr managed block"
FALLBACK_BEGIN_MARK="# BEGIN keen-pbr fallback upstream (added on removal; replace or remove)"
FALLBACK_END_MARK="# END keen-pbr fallback upstream"
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

conf_line() { printf 'conf-script=%s dnsmasq-config-entry' "$DNSMASQ_SH"; }

# Prints the managed block (4 lines).
block_text() {
    printf '%s\n%s\n%s\n%s\n' "$BEGIN_MARK" "$NOTICE_LINE" "$(conf_line)" "$END_MARK"
}

# block_present <file>: the file has a managed block with our conf-script line.
block_present() {
    [ -f "$1" ] || return 1
    awk -v begin="$BEGIN_MARK" -v end="$END_MARK" -v script="$(conf_line)" '
        $0 == begin { inside = 1; has = 0; next }
        inside && $0 == script { has = 1; next }
        inside && $0 == end { if (has) found = 1; inside = 0; next }
        END { exit found ? 0 : 1 }
    ' "$1"
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

usage() {
    echo "usage: $0 inspect | apply <selector> | remove <selector> | uninstall | install-recommended-config | status | alive [<selector>]" >&2
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
    _st=absent
    block_present "$DNSMASQ_CONF" && _st=current
    printf '[instance]\n'
    printf 'selector=%s\n' "$SELECTOR"
    printf 'enabled=%s\n' "$_en"
    printf 'exe_required=%s\n' "$DNSMASQ_BIN"
    printf 'match_conf_file=%s\n' "$DNSMASQ_CONF"
    printf 'default_conf_file=%s\n' "$DNSMASQ_CONF"
    printf 'dropin_include=managed\n'
    printf 'dropin_content=%s\n' "$(conf_line)"
    printf 'dropin_state=%s\n' "$_st"
}

do_restart() {
    eval "$RESTART_CMD" >/dev/null 2>&1 || die 1 "failed to restart dnsmasq"
}

# Appends the managed block when it is missing; nothing is written otherwise.
ensure_block() {
    block_present "$DNSMASQ_CONF" && return 0
    if grep -qxF "$BEGIN_MARK" "$DNSMASQ_CONF"; then
        die 1 "the keen-pbr block in $DNSMASQ_CONF is damaged; restore it or remove it and run apply again"
    fi
    {
        printf '\n'
        block_text
    } >> "$DNSMASQ_CONF" || die 1 "cannot update $DNSMASQ_CONF"
    log "added the managed block to $DNSMASQ_CONF"
}

do_apply() {
    check_selector "$1"
    if [ ! -f "$INIT_SCRIPT" ] || [ ! -f "$DNSMASQ_CONF" ]; then
        die 2 "dnsmasq is not installed (opkg install dnsmasq-full)"
    fi
    acquire_lock
    ensure_block
    do_restart
    log "applied"
}

do_remove() {
    check_selector "$1"
    acquire_lock
    if [ -f "$INIT_SCRIPT" ]; then
        do_restart
    fi
    log "removed"
}

# Prints dnsmasq.conf without the managed block.  When $1 is non-empty it is
# printed in the block's place (lines separated by a backslash and "n").
strip_block() {
    awk -v b="$BEGIN_MARK" -v e="$END_MARK" -v repl="$1" '
        $0 == b { skip = 1; if (repl != "") print repl; next }
        skip && $0 == e { skip = 0; next }
        !skip { print }
    ' "$DNSMASQ_CONF"
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
    status=0
    if [ -f "$DNSMASQ_CONF" ] && grep -qxF "$BEGIN_MARK" "$DNSMASQ_CONF"; then
        stripped="$(strip_block "")"
        repl=""
        if needs_fallback_upstream "$stripped"; then
            servers="$(grep '^[[:space:]]*server=' "$FALLBACK_CONF" 2>/dev/null || true)"
            [ -n "$servers" ] || servers="$DEFAULT_FALLBACK_SERVERS"
            # awk -v turns "\n" into a newline.
            repl="$FALLBACK_BEGIN_MARK"
            for srv in $servers; do repl="$repl\\n$srv"; done
            repl="$repl\\n$FALLBACK_END_MARK"
            log "dnsmasq has no upstream of its own; leaving the fallback servers in $DNSMASQ_CONF"
        fi
        tmp="$DNSMASQ_CONF.keen-pbr.tmp"
        if strip_block "$repl" > "$tmp" && cat "$tmp" > "$DNSMASQ_CONF"; then
            log "removed the managed block from $DNSMASQ_CONF"
            if [ -f "$INIT_SCRIPT" ]; then
                eval "$RESTART_CMD" >/dev/null 2>&1 || { log "uninstall: failed to restart dnsmasq"; status=1; }
            fi
        else
            log "uninstall: cannot update $DNSMASQ_CONF"
            status=1
        fi
        rm -f "$tmp"
    fi
    log "uninstalled"
    return "$status"
}

# Replaces dnsmasq.conf with the recommended one.  Nothing changes unless
# `dnsmasq --test` accepts the new file.
do_install_recommended() {
    if [ ! -x "$DNSMASQ_BIN" ] || [ ! -f "$INIT_SCRIPT" ]; then
        die 2 "dnsmasq is not installed (opkg install dnsmasq-full)"
    fi
    [ -f "$RECOMMENDED_CONF" ] || die 1 "$RECOMMENDED_CONF is missing"
    acquire_lock
    if [ -f "$DNSMASQ_CONF" ] && cmp -s "$RECOMMENDED_CONF" "$DNSMASQ_CONF"; then
        echo "$DNSMASQ_CONF already has the recommended settings; nothing changed."
        return 0
    fi
    candidate="$DNSMASQ_CONF.keen-pbr-new.$$"
    cp "$RECOMMENDED_CONF" "$candidate" || { rm -f "$candidate"; die 1 "cannot write $candidate"; }
    if ! out="$("$DNSMASQ_BIN" --test --conf-file="$candidate" 2>&1)"; then
        rm -f "$candidate"
        printf '%s\n' "$out" >&2
        die 1 "dnsmasq rejected the recommended settings; $DNSMASQ_CONF was not changed"
    fi
    if [ -f "$DNSMASQ_CONF" ]; then
        cp "$DNSMASQ_CONF" "$BACKUP_CONF" || { rm -f "$candidate"; die 1 "cannot write $BACKUP_CONF"; }
    fi
    mv -f "$candidate" "$DNSMASQ_CONF" || { rm -f "$candidate"; die 1 "cannot replace $DNSMASQ_CONF"; }
    log "installed the recommended dnsmasq settings"
    do_restart
    if [ -f "$BACKUP_CONF" ]; then
        echo "Replaced $DNSMASQ_CONF with the recommended settings. The previous file is saved as $BACKUP_CONF."
    else
        echo "Wrote the recommended settings to $DNSMASQ_CONF."
    fi
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
    uninstall) do_uninstall ;;
    install-recommended-config) do_install_recommended ;;
    status)
        if block_present "$DNSMASQ_CONF"; then
            echo installed
        else
            echo not-installed
        fi
        ;;
    alive) do_alive ;;
    *) usage ;;
esac
