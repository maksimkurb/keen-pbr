#!/bin/sh

set -eu

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
DNSMASQ_FALLBACK_FILE="/etc/keen-pbr/dnsmasq-fallback.conf"
STATE_DIR="/tmp/keen-pbr"
ACTIVE_FILE="${STATE_DIR}/active"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message"
}

log_warn() {
    log_message warn "$1"
}

log_info() {
    log_message info "$1"
}

resolver_type() {
    if command -v nft >/dev/null 2>&1; then
        echo "dnsmasq-nftset"
    else
        echo "dnsmasq-ipset"
    fi
}

fallback_conf_line() {
    printf 'conf-file=%s\n' "$DNSMASQ_FALLBACK_FILE"
}

active_conf_line() {
    printf 'conf-script=%s --config %s generate-resolver-config %s\n' \
        "$KEEN_PBR_BIN" "$CONFIG_PATH" "$(resolver_type)"
}

is_active() {
    [ -r "$ACTIVE_FILE" ] || return 1

    active_state="$(tr -d '[:space:]' < "$ACTIVE_FILE" 2>/dev/null || true)"
    [ "$active_state" = "Y" ]
}

set_active_state() {
    mkdir -p "$STATE_DIR"
    printf '%s\n' "$1" > "$ACTIVE_FILE"
}

emit_dnsmasq_config_entry() {
    if is_active; then
        active_conf_line
        log_info "Produced dnsmasq working config entry"
    else
        fallback_conf_line
        log_info "Produced dnsmasq fallback config entry"
    fi
}

activate_dnsmasq() {
    set_active_state "Y"
    log_info "Marked keen-pbr dnsmasq state as active"
    restart_dnsmasq
}

deactivate_dnsmasq() {
    set_active_state "N"
    log_info "Marked keen-pbr dnsmasq state as inactive"
    restart_dnsmasq
}

restart_dnsmasq() {
    if command -v systemctl >/dev/null 2>&1; then
        systemctl restart dnsmasq >/dev/null 2>&1 || true
    elif command -v service >/dev/null 2>&1; then
        service dnsmasq restart >/dev/null 2>&1 || true
    fi
}

print_help() {
    cat <<EOF
Usage: $0 <command>

Commands:
  dnsmasq-config-entry   Print the dnsmasq config entry for the current active state.
  activate               Mark keen-pbr dnsmasq state active and restart dnsmasq.
  deactivate             Mark keen-pbr dnsmasq state inactive and restart dnsmasq.
  restart-dnsmasq        Restart dnsmasq without changing helper-managed config.
  reload                 Alias for restart-dnsmasq; used by the system resolver hook.
  help                   Show this help text.
EOF
}

case "${1:-}" in
    dnsmasq-config-entry)
        emit_dnsmasq_config_entry
        ;;
    activate)
        activate_dnsmasq
        ;;
    deactivate)
        deactivate_dnsmasq
        ;;
    restart-dnsmasq)
        restart_dnsmasq
        ;;
    reload)
        restart_dnsmasq
        ;;
    help|-h|--help)
        print_help
        ;;
    *)
        print_help >&2
        exit 1
        ;;
esac
