#!/bin/sh

set -eu

KEEN_PBR_BIN="/opt/usr/bin/keen-pbr"
CONFIG_PATH="/opt/etc/keen-pbr/config.json"
DNSMASQ_CONF="/opt/etc/dnsmasq.conf"
DNSMASQ_TMP_DIR="/tmp/dnsmasq.d"
DNSMASQ_TMP_FILE="${DNSMASQ_TMP_DIR}/keen-pbr.conf"
DNSMASQ_FALLBACK_FILE="/opt/etc/keen-pbr/dnsmasq-fallback.conf"
BLOCK_START="# BEGIN keen-pbr managed block"
BLOCK_END="# END keen-pbr managed block"
BLOCK_LINE="conf-dir=/tmp/dnsmasq.d,*.conf"

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

append_managed_block() {
    mkdir -p "$(dirname "$DNSMASQ_CONF")"
    touch "$DNSMASQ_CONF"
    if [ -s "$DNSMASQ_CONF" ]; then
        printf '\n' >> "$DNSMASQ_CONF"
    fi
    printf '%s\n%s\n%s\n' "$BLOCK_START" "$BLOCK_LINE" "$BLOCK_END" >> "$DNSMASQ_CONF"
}

remove_managed_block() {
    [ -f "$DNSMASQ_CONF" ] || return 0
    awk -v start="$BLOCK_START" -v end="$BLOCK_END" '
        $0 == start { skip = 1; next }
        $0 == end { skip = 0; next }
        !skip { print }
    ' "$DNSMASQ_CONF" > "${DNSMASQ_CONF}.tmp" && mv "${DNSMASQ_CONF}.tmp" "$DNSMASQ_CONF"
}

install_persistent() {
    mkdir -p "$DNSMASQ_TMP_DIR"
    if [ -f "$DNSMASQ_FALLBACK_FILE" ] && [ ! -f "$DNSMASQ_TMP_FILE" ]; then
        cp "$DNSMASQ_FALLBACK_FILE" "$DNSMASQ_TMP_FILE"
    fi
}

ensure_runtime_prereqs() {
    install_persistent
    touch "$DNSMASQ_CONF"
    if ! grep -Fqx "$BLOCK_LINE" "$DNSMASQ_CONF"; then
        remove_managed_block
        append_managed_block
    fi
}

activate_dnsmasq() {
    mkdir -p "$DNSMASQ_TMP_DIR"
    conf_script_line > "$DNSMASQ_TMP_FILE"
    restart_dnsmasq
}

deactivate_dnsmasq() {
    if [ -f "$DNSMASQ_FALLBACK_FILE" ]; then
        cp "$DNSMASQ_FALLBACK_FILE" "$DNSMASQ_TMP_FILE"
    else
        rm -f "$DNSMASQ_TMP_FILE"
    fi
    restart_dnsmasq
}

uninstall_persistent() {
    remove_managed_block
    rm -f /opt/etc/dnsmasq.d/dnsmasq.keen-pbr.conf
    rm -f /etc/dnsmasq.d/keen-pbr.conf
    rm -f "$DNSMASQ_TMP_FILE"
}

restart_dnsmasq() {
    if [ -x /opt/etc/init.d/S56dnsmasq ]; then
        /opt/etc/init.d/S56dnsmasq restart 2>/dev/null || true
        return 0
    fi

    if command -v service >/dev/null 2>&1; then
        service dnsmasq restart 2>/dev/null || true
        return 0
    fi

    killall dnsmasq 2>/dev/null || true
}

case "$1" in
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
