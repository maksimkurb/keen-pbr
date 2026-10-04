#!/bin/ash
# keen-pbr dnsmasq hook (OpenWrt): dnsmasq-hook.sh apply|remove|status
#
# Makes dnsmasq run "keen-pbr generate-resolver-config dnsmasq" through a
# conf-script drop-in.  The drop-in lives in the (tmpfs) dnsmasq confdir; the
# only persistent change is the UCI addnmount list that exposes keen-pbr to
# the dnsmasq procd jail, and it is written only when an entry is missing.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/usr/sbin/keen-pbr}"
CONFIG_DIR="${CONFIG_DIR:-/etc/keen-pbr}"
CACHE_DIR="${CACHE_DIR:-/var/cache/keen-pbr}"
UCI="${UCI:-uci}"
RESTART_CMD="${RESTART_CMD:-/etc/init.d/dnsmasq restart}"
TMP_ROOT="${TMP_ROOT:-/tmp}"
CONF_NAME="keen-pbr-upstream-dns.conf"
JAIL_MOUNTS="$KEEN_PBR_BIN $CONFIG_DIR $CACHE_DIR"

conf_line() {
    printf 'conf-script=%s generate-resolver-config dnsmasq' "$KEEN_PBR_BIN"
}

log() {
    logger -t keen-pbr "dnsmasq-hook: $1" >/dev/null 2>&1 || true
}

die() {
    printf '%s\n' "$1" >&2
    log "$1"
    exit 1
}

dnsmasq_sections() {
    "$UCI" -q show dhcp | sed -n 's/^dhcp\.\([^.=]*\)=dnsmasq$/\1/p'
}

section_confdir() {
    _cd="$("$UCI" -q get "dhcp.$1.confdir" 2>/dev/null || true)"
    printf '%s\n' "${_cd:-$TMP_ROOT/dnsmasq.$1.d}"
}

# All candidate confdirs: per-section ones plus the well-known defaults.
all_conf_paths() {
    for _s in $(dnsmasq_sections); do
        printf '%s/%s\n' "$(section_confdir "$_s")" "$CONF_NAME"
    done
    for _p in "$TMP_ROOT/dnsmasq.d/$CONF_NAME" "$TMP_ROOT"/dnsmasq.*.d/"$CONF_NAME"; do
        [ -e "$_p" ] && printf '%s\n' "$_p"
    done
}

list_has() {
    for _i in $1; do
        [ "$_i" = "$2" ] && return 0
    done
    return 1
}

# Write the drop-in only when its content differs (tmpfs anyway).
write_conf() {
    _want="$(conf_line)"
    if [ -f "$1" ] && [ "$(cat "$1" 2>/dev/null)" = "$_want" ]; then
        return 0
    fi
    mkdir -p "$(dirname "$1")" || return 1
    printf '%s\n' "$_want" > "$1"
}

do_restart() {
    if ! eval "$RESTART_CMD" >/dev/null 2>&1; then
        die "failed to restart dnsmasq"
    fi
}

do_apply() {
    _sections="$(dnsmasq_sections)"
    [ -n "$_sections" ] || die "no dnsmasq section found in /etc/config/dhcp"

    # addnmount targets must exist before procd builds the jail.
    mkdir -p "$CACHE_DIR" "$CONFIG_DIR" 2>/dev/null || true

    _changed=1
    for _s in $_sections; do
        write_conf "$(section_confdir "$_s")/$CONF_NAME" ||
            die "cannot write $(section_confdir "$_s")/$CONF_NAME"
        _cur="$("$UCI" -q get "dhcp.$_s.addnmount" 2>/dev/null || true)"
        for _m in $JAIL_MOUNTS; do
            list_has "$_cur" "$_m" && continue
            "$UCI" -q add_list "dhcp.$_s.addnmount=$_m" ||
                die "uci add_list dhcp.$_s.addnmount failed"
            log "UCI add_list dhcp.$_s.addnmount=$_m"
            _changed=0
        done
    done
    if [ "$_changed" -eq 0 ]; then
        "$UCI" -q commit dhcp || die "uci commit dhcp failed"
    fi
    do_restart
    log "applied"
}

do_remove() {
    _changed=1
    for _p in $(all_conf_paths); do
        if [ -e "$_p" ]; then
            rm -f "$_p" && _changed=0
        fi
    done

    _ucichg=1
    for _s in $(dnsmasq_sections); do
        _cur="$("$UCI" -q get "dhcp.$_s.addnmount" 2>/dev/null || true)"
        for _m in $JAIL_MOUNTS; do
            list_has "$_cur" "$_m" || continue
            "$UCI" -q del_list "dhcp.$_s.addnmount=$_m" || die "uci del_list failed"
            log "UCI del_list dhcp.$_s.addnmount=$_m"
            _ucichg=0
        done
    done
    if [ "$_ucichg" -eq 0 ]; then
        "$UCI" -q commit dhcp || die "uci commit dhcp failed"
        _changed=0
    fi

    [ "$_changed" -eq 0 ] || return 0
    do_restart
    log "removed"
}

do_status() {
    for _p in $(all_conf_paths); do
        if [ -f "$_p" ]; then
            echo installed
            return 0
        fi
    done
    echo not-installed
}

case "$1" in
    apply) do_apply ;;
    remove) do_remove ;;
    status) do_status ;;
    *)
        echo "usage: $0 apply|remove|status" >&2
        exit 64
        ;;
esac
