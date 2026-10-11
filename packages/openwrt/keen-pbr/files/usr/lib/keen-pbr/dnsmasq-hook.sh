#!/bin/ash
# keen-pbr dnsmasq hook v2 (OpenWrt)
#   dnsmasq-hook.sh inspect
#   dnsmasq-hook.sh apply <selector>     selector: uci:<dhcp section name as in `uci -X show dhcp`>
#   dnsmasq-hook.sh remove <selector>
#   dnsmasq-hook.sh uninstall            (package removal: every section, no selector)
#   dnsmasq-hook.sh status               (v1 compatibility)
#   dnsmasq-hook.sh alive [<selector>]   (v1 compatibility; the daemon no longer calls it)
#
# apply installs a conf-script drop-in into the confdir of ONE section, adds the
# jail mounts keen-pbr needs to that section's addnmount list (recording every
# entry it adds in keen_pbr_addnmount), and restarts that instance only.
# remove deletes only the exact drop-in and the addnmount entries listed in
# keen_pbr_addnmount. Entries that existed before keen-pbr added them are never
# claimed. uninstall does the same for every section without running keen-pbr.
# Exit codes: 0 ok, 1 failure, 2 dnsmasq not installed, 3 unknown
# selector, 4 locked, 64 usage.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/usr/sbin/keen-pbr}"
CONFIG_DIR="${CONFIG_DIR:-/etc/keen-pbr}"
CACHE_DIR="${CACHE_DIR:-/var/cache/keen-pbr}"
UCI="${UCI:-uci}"
INIT_SCRIPT="${INIT_SCRIPT:-/etc/init.d/dnsmasq}"
RESTART_CMD="${RESTART_CMD:-$INIT_SCRIPT restart}"
# 1: "$RESTART_CMD <section>" restarts one instance (M0 experiment C2). 0: restart all.
PER_INSTANCE_RESTART="${PER_INSTANCE_RESTART:-1}"
ALIVE_CMD="${ALIVE_CMD:-}"
TMP_ROOT="${TMP_ROOT:-/tmp}"
RUNTIME_CONF_BASE="${RUNTIME_CONF_BASE:-/var/etc/dnsmasq.conf}"
DNSMASQ_BIN="${DNSMASQ_BIN:-}"
LOCK_DIR="${LOCK_DIR:-/tmp/keen-pbr-dnsmasq-hook.lock}"
LOCK_TRIES="${LOCK_TRIES:-30}"
# `uninstall` gives up waiting sooner and then goes on without the lock.
UNINSTALL_LOCK_TRIES="${UNINSTALL_LOCK_TRIES:-5}"
CONF_NAME="keen-pbr-upstream-dns.conf"
JAIL_MOUNTS="$KEEN_PBR_BIN $CONFIG_DIR $CACHE_DIR"
OWN_OPT="keen_pbr_addnmount"

conf_line() {
    printf 'conf-script=%s generate-resolver-config dnsmasq' "$KEEN_PBR_BIN"
}

log() { logger -t keen-pbr "dnsmasq-hook: $1" >/dev/null 2>&1 || true; }

die() {
    _code="$1"; shift
    printf '%s\n' "$*" >&2
    log "$*"
    exit "$_code"
}

usage() {
    echo "usage: $0 inspect | apply <selector> | remove <selector> | uninstall | status | alive [<selector>]" >&2
    exit 64
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

# dropin_state <file> <expected-line>: prints absent|current|foreign
dropin_state() {
    if [ ! -e "$1" ]; then echo absent; return 0; fi
    if [ "$(cat "$1" 2>/dev/null)" = "$2" ]; then echo current; return 0; fi
    echo foreign
}

dnsmasq_sections() {
    "$UCI" -q -X show dhcp | sed -n 's/^dhcp\.\([^.=]*\)=dnsmasq$/\1/p'
}

# Directory part of the confdir option; "dir,*.conf" -> "dir".
section_confdir() {
    _cd="$("$UCI" -q get "dhcp.$1.confdir" 2>/dev/null || true)"
    _cd="${_cd%%,*}"
    printf '%s\n' "${_cd:-$TMP_ROOT/dnsmasq.$1.d}"
}

list_has() {
    for _i in $1; do
        [ "$_i" = "$2" ] && return 0
    done
    return 1
}

# selector_section <selector>: prints the section; returns 3 when the selector
# is malformed or the section is not a dnsmasq section.
selector_section() {
    case "$1" in
        uci:?*) _sec="${1#uci:}" ;;
        *) return 3 ;;
    esac
    case "$_sec" in *[!A-Za-z0-9_]*) return 3 ;; esac
    [ "$("$UCI" -q get "dhcp.$_sec" 2>/dev/null)" = dnsmasq ] || return 3
    printf '%s\n' "$_sec"
}

# `uci commit dhcp` would also commit somebody else's pending edits.
require_clean_uci() {
    if [ -n "$("$UCI" -q changes dhcp 2>/dev/null)" ]; then
        die 1 "the dhcp UCI package has uncommitted changes; commit or revert them first"
    fi
}

restart_instance() {
    if [ "$PER_INSTANCE_RESTART" = 1 ]; then
        eval "$RESTART_CMD" '"$1"' >/dev/null 2>&1 || die 1 "failed to restart dnsmasq instance $1"
    else
        eval "$RESTART_CMD" >/dev/null 2>&1 || die 1 "failed to restart dnsmasq"
    fi
}

do_inspect() {
    printf 'hook_version=2\nplatform=openwrt\n'
    _bin="${DNSMASQ_BIN:-$(command -v dnsmasq 2>/dev/null || true)}"
    printf 'binary=%s\n' "$_bin"
    [ -n "$_bin" ] || exit 2
    _svc_enabled=1
    if [ -x "$INIT_SCRIPT" ] && ! "$INIT_SCRIPT" enabled >/dev/null 2>&1; then
        _svc_enabled=0
    fi
    _want="$(conf_line)"
    for _s in $(dnsmasq_sections); do
        _en="$_svc_enabled"
        case "$("$UCI" -q get "dhcp.$_s.disabled" 2>/dev/null)" in
            1|yes|on|true|enabled) _en=0 ;;
        esac
        _dir="$(section_confdir "$_s")"
        _f="$_dir/$CONF_NAME"
        _cur="$("$UCI" -q get "dhcp.$_s.addnmount" 2>/dev/null || true)"
        _missing=""
        for _m in $JAIL_MOUNTS; do
            list_has "$_cur" "$_m" || _missing="${_missing:+$_missing }$_m"
        done
        printf '[instance]\n'
        printf 'selector=uci:%s\n' "$_s"
        printf 'enabled=%s\n' "$_en"
        printf 'match_conf_file=%s.%s\n' "$RUNTIME_CONF_BASE" "$_s"
        printf 'dropin_include=static\n'
        printf 'dropin_dir=%s\n' "$_dir"
        printf 'dropin_file=%s\n' "$_f"
        printf 'dropin_content=%s\n' "$_want"
        printf 'dropin_state=%s\n' "$(dropin_state "$_f" "$_want")"
        printf 'owned_mounts=%s\n' "$("$UCI" -q get "dhcp.$_s.$OWN_OPT" 2>/dev/null || true)"
        printf 'missing_mounts=%s\n' "$_missing"
    done
}

do_apply() {
    [ -n "$1" ] || usage
    _s="$(selector_section "$1")" || die 3 "unknown dnsmasq selector: $1"
    acquire_lock
    mkdir -p "$CACHE_DIR" "$CONFIG_DIR" 2>/dev/null || true   # addnmount targets must exist
    _want="$(conf_line)"
    _f="$(section_confdir "$_s")/$CONF_NAME"
    if [ "$(dropin_state "$_f" "$_want")" = foreign ]; then
        die 1 "$_f exists and was not written by keen-pbr; not overwriting it"
    fi
    _cur="$("$UCI" -q get "dhcp.$_s.addnmount" 2>/dev/null || true)"
    _need=""
    for _m in $JAIL_MOUNTS; do
        list_has "$_cur" "$_m" || _need="${_need:+$_need }$_m"
    done
    [ -z "$_need" ] || require_clean_uci
    write_line_atomic "$_f" "$_want" || die 1 "cannot write $_f"
    for _m in $_need; do
        "$UCI" -q add_list "dhcp.$_s.addnmount=$_m" || die 1 "uci add_list dhcp.$_s.addnmount failed"
        "$UCI" -q add_list "dhcp.$_s.$OWN_OPT=$_m" || die 1 "uci add_list dhcp.$_s.$OWN_OPT failed"
        log "UCI add_list dhcp.$_s.addnmount=$_m (owned by keen-pbr)"
    done
    if [ -n "$_need" ]; then
        "$UCI" -q commit dhcp || die 1 "uci commit dhcp failed"
    fi
    restart_instance "$_s"
    log "applied to uci:$_s"
}

do_remove() {
    [ -n "$1" ] || usage
    _s="$(selector_section "$1")" || die 3 "unknown dnsmasq selector: $1"
    acquire_lock
    _changed=1
    _f="$(section_confdir "$_s")/$CONF_NAME"
    if [ "$(dropin_state "$_f" "$(conf_line)")" = current ]; then
        rm -f "$_f" || die 1 "cannot remove $_f"
        _changed=0
    fi
    _owned="$("$UCI" -q get "dhcp.$_s.$OWN_OPT" 2>/dev/null || true)"
    if [ -n "$_owned" ]; then
        require_clean_uci
        for _m in $_owned; do
            "$UCI" -q del_list "dhcp.$_s.addnmount=$_m" || die 1 "uci del_list dhcp.$_s.addnmount failed"
            log "UCI del_list dhcp.$_s.addnmount=$_m"
        done
        "$UCI" -q delete "dhcp.$_s.$OWN_OPT" || die 1 "uci delete dhcp.$_s.$OWN_OPT failed"
        "$UCI" -q commit dhcp || die 1 "uci commit dhcp failed"
        _changed=0
    fi
    [ "$_changed" -eq 0 ] || return 0
    restart_instance "$_s"
    log "removed from uci:$_s"
}

# own_dropin <file>: the file holds only conf-script lines that run keen-pbr
# (any version), nothing written by somebody else.
own_dropin() {
    [ -f "$1" ] || return 1
    awk '
        /^[ \t]*$/ { next }
        /^conf-script=[^ ]*keen-pbr/ { found = 1; next }
        { foreign = 1 }
        END { exit((found && !foreign) ? 0 : 1) }
    ' "$1"
}

# Package removal.  Never runs keen-pbr (it may be broken or already gone),
# does not stop at the first failure and does not wait forever for the lock:
# the daemon is stopped by then, so a held lock is only a stuck hook.
# The jail mounts are removed even when dhcp has uncommitted changes (they get
# committed too): a mount of a path the package deletes can keep the jailed
# dnsmasq from starting.  keen-pbr never changes the upstream servers, so
# removing the drop-in gives every instance its own upstreams back.
do_uninstall() {
    take_lock "$UNINSTALL_LOCK_TRIES" || log "uninstall: the lock is held; continuing without it"
    _status=0
    _changed=0
    _commit=0
    for _s in $(dnsmasq_sections); do
        _f="$(section_confdir "$_s")/$CONF_NAME"
        if own_dropin "$_f"; then
            if rm -f "$_f"; then _changed=1; else _status=1; fi
        fi
        _owned="$("$UCI" -q get "dhcp.$_s.$OWN_OPT" 2>/dev/null || true)"
        [ -n "$_owned" ] || continue
        for _m in $_owned; do
            "$UCI" -q del_list "dhcp.$_s.addnmount=$_m" || _status=1
        done
        "$UCI" -q delete "dhcp.$_s.$OWN_OPT" || _status=1
        _commit=1
    done
    if [ "$_commit" -eq 1 ]; then
        "$UCI" -q commit dhcp || _status=1
        log "uninstall: removed the jail mounts keen-pbr added"
        _changed=1
    fi
    if [ "$_changed" -eq 1 ]; then
        eval "$RESTART_CMD" >/dev/null 2>&1 || { log "uninstall: failed to restart dnsmasq"; _status=1; }
    fi
    log "uninstalled"
    return "$_status"
}

do_status() {
    _want="$(conf_line)"
    for _s in $(dnsmasq_sections); do
        if [ "$(dropin_state "$(section_confdir "$_s")/$CONF_NAME" "$_want")" = current ]; then
            echo installed
            return 0
        fi
    done
    echo not-installed
}

# Exit 0: dnsmasq runs, 1: it does not, 2: unknown.
# procd's rc.common provides `<init script> running` (exit 0 while an instance
# of the service is registered with procd).  Older/odd rc.common builds lack
# it, so a non-zero answer is double-checked with pidof: only when neither
# says dnsmasq is up is it reported dead.
do_alive() {
    if [ -n "$ALIVE_CMD" ]; then
        eval "$ALIVE_CMD" >/dev/null 2>&1
        case "$?" in
            0) return 0 ;;
            1) return 1 ;;
            *) return 2 ;;
        esac
    fi
    if [ -x "$INIT_SCRIPT" ] && "$INIT_SCRIPT" running >/dev/null 2>&1; then
        return 0
    fi
    command -v pidof >/dev/null 2>&1 || return 2
    pidof dnsmasq >/dev/null 2>&1 && return 0
    return 1
}

case "${1:-}" in
    inspect) do_inspect ;;
    apply) do_apply "${2:-}" ;;
    remove) do_remove "${2:-}" ;;
    uninstall) do_uninstall ;;
    status) do_status ;;
    alive) do_alive ;;
    *) usage ;;
esac
