#!/bin/sh

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
PACKAGE_NAME="keen-pbr"
DNSMASQ_FILE="/var/run/${PACKAGE_NAME}.dnsmasq"

resolver_type() {
    if command -v nft >/dev/null 2>&1; then
        echo "dnsmasq-nftset"
    else
        echo "dnsmasq-ipset"
    fi
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

dnsmasq_instance_get_confdir() {
    local section="$1"
    local cfg cfg_file

    cfg="$(uci -q show "dhcp.${section}" | awk -F'[.=]' 'NR==1{print $2}')"
    [ -n "$cfg" ] || return 1

    cfg_file="$(ubus call service list '{"name":"dnsmasq"}' 2>/dev/null |
        jsonfilter -e "@.dnsmasq.instances.${cfg}.command" |
        awk '{gsub(/\\\//,"/");gsub(/[][\",]/,"");for(i=1;i<=NF;i++)if($i==\"-C\"){print $(i+1);exit}}')"

    [ -n "$cfg_file" ] && [ -f "$cfg_file" ] || return 1
    awk -F= '/^conf-dir=/{print $2; exit}' "$cfg_file"
}

dnsmasq_sections() {
    uci -q show dhcp | sed -n 's/^dhcp\.\([^.=][^.=]*\)=dnsmasq$/\1/p'
}

dnsmasq_instance_setup() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_instance_get_confdir "$section")"
    [ -n "$confdir" ] || return 1

    mkdir -p "$confdir"
    uci_add_list_if_new dhcp "$section" addnmount "$DNSMASQ_FILE"
    ln -sf "$DNSMASQ_FILE" "${confdir}/${PACKAGE_NAME}"
    chmod 660 "${confdir}/${PACKAGE_NAME}" 2>/dev/null || true
    chown -h root:dnsmasq "${confdir}/${PACKAGE_NAME}" 2>/dev/null || true
}

dnsmasq_instance_cleanup() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_instance_get_confdir "$section")"
    [ -n "$confdir" ] && rm -f "${confdir}/${PACKAGE_NAME}"
    uci -q del_list "dhcp.${section}.addnmount=${DNSMASQ_FILE}"
}

configure_dnsmasq() {
    local section

    mkdir -p "${DNSMASQ_FILE%/*}"
    cat >"$DNSMASQ_FILE" <<EOF
# Managed by keen-pbr. Removed automatically on service stop.
conf-script=${KEEN_PBR_BIN} --config ${CONFIG_PATH} generate-resolver-config $(resolver_type)
EOF

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
    rm -f "$DNSMASQ_FILE"
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
