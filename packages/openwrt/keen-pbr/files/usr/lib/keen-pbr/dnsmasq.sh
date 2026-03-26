#!/bin/sh

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_DIR="/etc/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
CACHE_DIR="/var/cache/keen-pbr"
PACKAGE_NAME="keen-pbr"
CONFFILE="${PACKAGE_NAME}.conf"

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

cleanup_dnsmasq() {
    local section confdir

    for section in $(dnsmasq_sections); do
        confdir="$(dnsmasq_confdir "$section")"
        rm -f "${confdir}/${CONFFILE}"
    done
}

reload_dnsmasq() {
    /etc/init.d/dnsmasq restart 2>/dev/null || true
}

case "$1" in
    configure)
        configure_dnsmasq
        ;;
    cleanup)
        cleanup_dnsmasq
        ;;
    restore)
        restore_dnsmasq
        ;;
    reload)
        reload_dnsmasq
        ;;
    *)
        echo "Usage: $0 {configure|cleanup|restore|reload}" >&2
        exit 1
        ;;
esac
