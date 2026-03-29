#!/bin/sh

set -eu

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
DNSMASQ_MANAGED_FILE="/etc/dnsmasq.d/keen-pbr.conf"

resolver_type() {
    if command -v nft >/dev/null 2>&1; then
        echo "dnsmasq-nftset"
    else
        echo "dnsmasq-ipset"
    fi
}

conf_script_line() {
    printf 'conf-script=%s --config %s generate-resolver-config %s\n' \
        "$KEEN_PBR_BIN" "$CONFIG_PATH" "$(resolver_type)"
}

configure_dnsmasq() {
    mkdir -p "$(dirname "$DNSMASQ_MANAGED_FILE")"
    conf_script_line > "$DNSMASQ_MANAGED_FILE"
}

cleanup_dnsmasq() {
    rm -f "$DNSMASQ_MANAGED_FILE"
}

restore_dnsmasq() {
    cleanup_dnsmasq
}

reload_dnsmasq() {
    if command -v systemctl >/dev/null 2>&1; then
        systemctl reload dnsmasq >/dev/null 2>&1 || systemctl restart dnsmasq >/dev/null 2>&1 || true
    elif command -v service >/dev/null 2>&1; then
        service dnsmasq reload >/dev/null 2>&1 || service dnsmasq restart >/dev/null 2>&1 || true
    fi
}

case "${1:-}" in
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
