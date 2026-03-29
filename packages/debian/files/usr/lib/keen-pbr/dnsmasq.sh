#!/bin/sh

set -eu

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
DNSMASQ_PERSISTENT_FILE="/etc/dnsmasq.d/keen-pbr-tmpdir.conf"
DNSMASQ_TMP_DIR="/tmp/dnsmasq.d"
DNSMASQ_TMP_FILE="${DNSMASQ_TMP_DIR}/keen-pbr.conf"

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

install_persistent() {
    mkdir -p "$(dirname "$DNSMASQ_PERSISTENT_FILE")"
    printf '%s\n' "conf-dir=${DNSMASQ_TMP_DIR},*.conf" > "$DNSMASQ_PERSISTENT_FILE"
}

ensure_runtime_prereqs() {
    install_persistent
}

activate_dnsmasq() {
    mkdir -p "$DNSMASQ_TMP_DIR"
    conf_script_line > "$DNSMASQ_TMP_FILE"
    restart_dnsmasq
}

deactivate_dnsmasq() {
    rm -f "$DNSMASQ_TMP_FILE"
    restart_dnsmasq
}

uninstall_persistent() {
    rm -f "$DNSMASQ_PERSISTENT_FILE"
    rm -f "$DNSMASQ_TMP_FILE"
}

restart_dnsmasq() {
    if command -v systemctl >/dev/null 2>&1; then
        systemctl restart dnsmasq >/dev/null 2>&1 || true
    elif command -v service >/dev/null 2>&1; then
        service dnsmasq restart >/dev/null 2>&1 || true
    fi
}

case "${1:-}" in
    install-persistent)
        install_persistent
        ;;
    ensure-runtime-prereqs)
        ensure_runtime_prereqs
        ;;
    activate)
        activate_dnsmasq
        ;;
    deactivate)
        deactivate_dnsmasq
        ;;
    uninstall-persistent)
        uninstall_persistent
        ;;
    restart-dnsmasq)
        restart_dnsmasq
        ;;
    configure)
        activate_dnsmasq
        ;;
    cleanup)
        deactivate_dnsmasq
        ;;
    restore)
        uninstall_persistent
        ;;
    reload)
        restart_dnsmasq
        ;;
    *)
        echo "Usage: $0 {install-persistent|ensure-runtime-prereqs|activate|deactivate|uninstall-persistent|restart-dnsmasq}" >&2
        exit 1
        ;;
esac
