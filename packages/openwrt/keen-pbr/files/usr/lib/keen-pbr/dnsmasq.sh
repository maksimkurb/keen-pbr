#!/bin/sh

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
PACKAGE_NAME="keen-pbr"
EXTRACONF_BEGIN="# ${PACKAGE_NAME} begin"
EXTRACONF_END="# ${PACKAGE_NAME} end"

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

set_dnsmasq_extraconftext() {
    local section="$1"
    local current cleaned block updated

    current="$(uci -q get "dhcp.${section}.extraconftext")"
    cleaned="$(printf '%s\n' "$current" | strip_keen_pbr_block)"
    block="${EXTRACONF_BEGIN}
$(conf_script_line)
${EXTRACONF_END}"

    if [ -n "$cleaned" ]; then
        updated="${cleaned}
${block}"
    else
        updated="${block}"
    fi

    uci -q set "dhcp.${section}.extraconftext=${updated}"
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

dnsmasq_instance_setup() {
    local section="$1"

    uci_add_list_if_new dhcp "$section" addnmount "$KEEN_PBR_BIN"
    uci_add_list_if_new dhcp "$section" addnmount "$CONFIG_PATH"
    set_dnsmasq_extraconftext "$section"
}

dnsmasq_instance_cleanup() {
    local section="$1"

    uci -q del_list "dhcp.${section}.addnmount=${KEEN_PBR_BIN}"
    uci -q del_list "dhcp.${section}.addnmount=${CONFIG_PATH}"
    clear_dnsmasq_extraconftext "$section"
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
    /etc/init.d/dnsmasq reload 2>/dev/null || /etc/init.d/dnsmasq restart 2>/dev/null || true
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
