#!/bin/ash
# keen-pbr dnsmasq hook v2 (OpenWrt)
#   dnsmasq-hook.sh inspect
#   dnsmasq-hook.sh apply <selector> [--default-upstreams=yes|no]
#   dnsmasq-hook.sh remove <selector>
#   dnsmasq-hook.sh uninstall            (package removal: every section, no selector)
#   dnsmasq-hook.sh boot                 (init script: rewrite the drop-ins in /tmp)
#   dnsmasq-hook.sh status               (v1 compatibility)
#   dnsmasq-hook.sh alive [<selector>]   (v1 compatibility; the daemon no longer calls it)
#
# The drop-in keen-pbr.conf in the section's confdir holds one conf-script line
# that runs `keen-pbr generate-resolver-config dnsmasq`; dnsmasq runs it on
# every start and the binary decides from config.json what to print.  The
# jail cannot see /usr/lib/keen-pbr, so the line calls the binary directly.
#
# apply <selector>    writes the drop-in (when missing or different), adds the
#                     three jail mounts (/usr/sbin/keen-pbr, /etc/keen-pbr,
#                     /var/cache/keen-pbr) to addnmount when missing, removes
#                     the 3.3.1 mount /var/run/keen-pbr, syncs the server
#                     lists (below), commits UCI once and only if something
#                     changed, restarts that instance.
#                     --default-upstreams=yes|no tells whether dns.fallback is
#                     set (the daemon passes it); it is `no` when omitted.
# remove <selector>   integration switched off: gives the servers back,
#                     commits when needed, restarts that instance.  The drop-in
#                     and the mounts stay.
# uninstall           gives the servers back, removes the mounts, the 3.3.1
#                     mount and the drop-in of every section, one commit,
#                     restart.  Never runs keen-pbr.
#
# Server lists.  While keen-pbr is on AND default upstreams are set, the
# unscoped `server` entries (not starting with "/") of the section move to
# `kpbr_server`, so dnsmasq does not ask them next to keen-pbr's default
# servers.  Scoped entries (/lan/192.168.1.1, /local/) always stay in `server`;
# scoped entries an older version stashed are put back.  Otherwise everything
# in `kpbr_server` goes back to `server`.
#
# Exit codes: 0 ok, 1 failure, 2 dnsmasq not installed, 3 unknown
# selector, 4 locked, 64 usage.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/usr/sbin/keen-pbr}"
CONFIG_DIR="${CONFIG_DIR:-/etc/keen-pbr}"
CACHE_DIR="${CACHE_DIR:-/var/cache/keen-pbr}"
# Mounted by 3.3.1 for the control socket; no longer needed.
LEGACY_RUN_DIR="${LEGACY_RUN_DIR:-/var/run/keen-pbr}"
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
CONF_NAME="keen-pbr.conf"
JAIL_MOUNTS="$KEEN_PBR_BIN $CONFIG_DIR $CACHE_DIR"
STASH_OPT="kpbr_server"

# Set when a UCI edit was made; `uci commit dhcp` runs once at the end.
_dirty=0
_checked_clean=0

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
    echo "usage: $0 inspect | apply <selector> [--default-upstreams=yes|no] | remove <selector> | uninstall | boot | status | alive [<selector>]" >&2
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

# `uci commit dhcp` would also commit somebody else's pending edits.  Checked
# once, before the first edit.
begin_edit() {
    if [ "$_checked_clean" -eq 0 ]; then
        if [ -n "$("$UCI" -q changes dhcp 2>/dev/null)" ]; then
            die 1 "the dhcp UCI package has uncommitted changes; commit or revert them first"
        fi
        _checked_clean=1
    fi
    _dirty=1
}

uci_add_list() {
    begin_edit
    "$UCI" -q add_list "dhcp.$1.$2=$3" || die 1 "uci add_list dhcp.$1.$2 failed"
    log "UCI add_list dhcp.$1.$2=$3"
}

uci_del_list() {
    begin_edit
    "$UCI" -q del_list "dhcp.$1.$2=$3" || die 1 "uci del_list dhcp.$1.$2 failed"
    log "UCI del_list dhcp.$1.$2=$3"
}

uci_delete() {
    begin_edit
    "$UCI" -q delete "dhcp.$1.$2" || die 1 "uci delete dhcp.$1.$2 failed"
    log "UCI delete dhcp.$1.$2"
}

commit_if_dirty() {
    [ "$_dirty" -eq 1 ] || return 0
    "$UCI" -q commit dhcp || return 1
    _dirty=0
}

# sync_servers <section> <move>: move=1 stashes the unscoped servers, 0 gives
# everything back.  Edits UCI only when a list has to change.
sync_servers() {
    _s="$1"; _move="$2"
    _servers="$("$UCI" -q get "dhcp.$_s.server" 2>/dev/null || true)"
    _stash="$("$UCI" -q get "dhcp.$_s.$STASH_OPT" 2>/dev/null || true)"
    if [ "$_move" -eq 1 ]; then
        for _e in $_stash; do
            case "$_e" in /*)
                list_has "$_servers" "$_e" || uci_add_list "$_s" server "$_e"
                _servers="$_servers $_e"
                uci_del_list "$_s" "$STASH_OPT" "$_e"
                ;;
            esac
        done
        for _e in $_servers; do
            case "$_e" in /*) continue ;; esac
            list_has "$_stash" "$_e" || { uci_add_list "$_s" "$STASH_OPT" "$_e"; _stash="$_stash $_e"; }
            uci_del_list "$_s" server "$_e"
        done
    else
        for _e in $_stash; do
            list_has "$_servers" "$_e" || { uci_add_list "$_s" server "$_e"; _servers="$_servers $_e"; }
        done
        [ -z "$_stash" ] || uci_delete "$_s" "$STASH_OPT"
    fi
}

# add_mounts <section>: the three jail mounts; drops the 3.3.1 one.
add_mounts() {
    _cur="$("$UCI" -q get "dhcp.$1.addnmount" 2>/dev/null || true)"
    for _m in $JAIL_MOUNTS; do
        list_has "$_cur" "$_m" || uci_add_list "$1" addnmount "$_m"
    done
    ! list_has "$_cur" "$LEGACY_RUN_DIR" || uci_del_list "$1" addnmount "$LEGACY_RUN_DIR"
}

# remove_mounts <section>: exactly our mounts.
remove_mounts() {
    _cur="$("$UCI" -q get "dhcp.$1.addnmount" 2>/dev/null || true)"
    for _m in $JAIL_MOUNTS $LEGACY_RUN_DIR; do
        ! list_has "$_cur" "$_m" || uci_del_list "$1" addnmount "$_m"
    done
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
        printf 'missing_mounts=%s\n' "$_missing"
    done
}

do_apply() {
    [ -n "$1" ] || usage
    _s="$(selector_section "$1")" || die 3 "unknown dnsmasq selector: $1"
    _move=0
    shift
    for _arg in "$@"; do
        case "$_arg" in
            --default-upstreams=yes) _move=1 ;;
            --default-upstreams=no) _move=0 ;;
        esac
    done
    acquire_lock
    mkdir -p "$CACHE_DIR" "$CONFIG_DIR" 2>/dev/null || true   # addnmount targets must exist
    _want="$(conf_line)"
    _f="$(section_confdir "$_s")/$CONF_NAME"
    if [ "$(dropin_state "$_f" "$_want")" = foreign ]; then
        die 1 "$_f exists and was not written by keen-pbr; not overwriting it"
    fi
    add_mounts "$_s"
    sync_servers "$_s" "$_move"
    commit_if_dirty || die 1 "uci commit dhcp failed"
    write_line_atomic "$_f" "$_want" || die 1 "cannot write $_f"
    restart_instance "$_s"
    log "applied to uci:$_s"
}

do_remove() {
    [ -n "$1" ] || usage
    _s="$(selector_section "$1")" || die 3 "unknown dnsmasq selector: $1"
    acquire_lock
    sync_servers "$_s" 0
    commit_if_dirty || die 1 "uci commit dhcp failed"
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
# dnsmasq from starting.
do_uninstall() {
    take_lock "$UNINSTALL_LOCK_TRIES" || log "uninstall: the lock is held; continuing without it"
    _status=0
    _changed=0
    _checked_clean=1   # commit even over pending edits, see above
    for _s in $(dnsmasq_sections); do
        _f="$(section_confdir "$_s")/$CONF_NAME"
        if own_dropin "$_f"; then
            if rm -f "$_f"; then _changed=1; else _status=1; fi
        fi
        sync_servers "$_s" 0
        remove_mounts "$_s"
    done
    if [ "$_dirty" -eq 1 ]; then
        commit_if_dirty || _status=1
        log "uninstall: gave the servers back and removed the mounts keen-pbr added"
        _changed=1
    fi
    if [ "$_changed" -eq 1 ]; then
        eval "$RESTART_CMD" >/dev/null 2>&1 || { log "uninstall: failed to restart dnsmasq"; _status=1; }
    fi
    log "uninstalled"
    return "$_status"
}

# Boot: /tmp is empty again.  Rewrites the drop-in of every section that has
# our mounts (so dnsmasq finds it on its first start) and creates the mount
# targets.  Writes to /tmp only; never restarts anything.
do_boot() {
    _want="$(conf_line)"
    for _s in $(dnsmasq_sections); do
        _cur="$("$UCI" -q get "dhcp.$_s.addnmount" 2>/dev/null || true)"
        list_has "$_cur" "$KEEN_PBR_BIN" || continue
        mkdir -p "$CACHE_DIR" "$CONFIG_DIR" 2>/dev/null || true
        # A 3.3.1 install still lists this mount until the first apply.
        ! list_has "$_cur" "$LEGACY_RUN_DIR" || mkdir -p "$LEGACY_RUN_DIR" 2>/dev/null || true
        _f="$(section_confdir "$_s")/$CONF_NAME"
        [ "$(dropin_state "$_f" "$_want")" = foreign ] && continue
        write_line_atomic "$_f" "$_want" || log "boot: cannot write $_f"
    done
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
    apply) shift; do_apply "$@" ;;
    remove) do_remove "${2:-}" ;;
    uninstall) do_uninstall ;;
    boot) do_boot ;;
    status) do_status ;;
    alive) do_alive ;;
    *) usage ;;
esac
