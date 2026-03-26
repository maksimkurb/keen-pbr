#!/bin/sh

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_DIR="/etc/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
CACHE_DIR="/var/cache/keen-pbr"
PACKAGE_NAME="keen-pbr"
CONFFILE="${PACKAGE_NAME}.conf"
EXTRACONF_BEGIN="# ${PACKAGE_NAME} begin"
EXTRACONF_END="# ${PACKAGE_NAME} end"

# Paths to bind-mount read-only into the dnsmasq procd jail
JAIL_MOUNTS="$KEEN_PBR_BIN $CONFIG_DIR $CACHE_DIR"

resolver_type() {
    if command -v nft >/dev/null 2>&1; then
        echo "dnsmasq-nftset"
    else
        echo "dnsmasq-ipset"
    fi
}

conf_script_line() {
    printf 'conf-script=%s --config %s generate-resolver-config %s' \
        "$KEEN_PBR_BIN" "$CONFIG_PATH" "$(resolver_type)"
}

uci_add_list_if_new() {
    local package="$1"
    local config="$2"
    local option="$3"
    local value="$4"
    local current item

    current="$(uci -q get "${package}.${config}.${option}")"
    for item in $current; do
        [ "$item" = "$value" ] && return 0
    done

    uci -q add_list "${package}.${config}.${option}=${value}"
}

strip_keen_pbr_block() {
    awk -v begin="$EXTRACONF_BEGIN" -v end="$EXTRACONF_END" '
        $0 == begin { skip = 1; next }
        $0 == end { skip = 0; next }
        !skip { print }
    '
}

clear_dnsmasq_extraconftext() {
    local section="$1"
    local current cleaned

    current="$(uci -q get "dhcp.${section}.extraconftext")"
    cleaned="$(printf '%s\n' "$current" | strip_keen_pbr_block)"

    if [ -n "$cleaned" ]; then
        uci -q set "dhcp.${section}.extraconftext=${cleaned}"
    else
        uci -q delete "dhcp.${section}.extraconftext"
    fi
}

dnsmasq_sections() {
    uci -q show dhcp | sed -n 's/^dhcp\.\([^.=][^.=]*\)=dnsmasq$/\1/p'
}

dnsmasq_confdir() {
    local section="$1"
    local confdir
    confdir="$(uci -q get "dhcp.${section}.confdir")"
    printf '%s' "${confdir:-/tmp/dnsmasq.${section}.d}"
}

dnsmasq_instance_setup() {
    local section="$1"
    local path confdir

    for path in $JAIL_MOUNTS; do
        uci_add_list_if_new dhcp "$section" addnmount "$path"
    done

    confdir="$(dnsmasq_confdir "$section")"
    mkdir -p "$confdir"
    printf '%s\n' "$(conf_script_line)" > "${confdir}/${CONFFILE}"
}

dnsmasq_instance_cleanup() {
    local section="$1"
    local path confdir

    for path in $JAIL_MOUNTS; do
        uci -q del_list "dhcp.${section}.addnmount=${path}"
    done
    # Legacy cleanup for versions that mounted CONFIG_PATH or KEEN_PBR_DIR
    uci -q del_list "dhcp.${section}.addnmount=${CONFIG_PATH}"
    uci -q del_list "dhcp.${section}.addnmount=/usr/lib/keen-pbr"
    # Legacy cleanup for versions that used extraconftext
    clear_dnsmasq_extraconftext "$section"

    confdir="$(dnsmasq_confdir "$section")"
    rm -f "${confdir}/${CONFFILE}"
}

configure_dnsmasq() {
    local section

    for section in $(dnsmasq_sections); do
        dnsmasq_instance_setup "$section" || true
    done

    uci -q commit dhcp || true
}

restore_dnsmasq() {
    local section

    for section in $(dnsmasq_sections); do
        dnsmasq_instance_cleanup "$section" || true
    done

    uci -q commit dhcp || true
}

reload_dnsmasq() {
    /etc/init.d/dnsmasq restart 2>/dev/null || true
}

case "$1" in
    configure)
        configure_dnsmasq
        ;;
    restore)
        restore_dnsmasq
        ;;
    reload)
        reload_dnsmasq
        ;;
    *)
        echo "Usage: $0 {configure|restore|reload}" >&2
        exit 1
        ;;
esac
