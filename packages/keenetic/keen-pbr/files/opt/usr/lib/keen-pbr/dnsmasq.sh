#!/bin/sh

set -e

KEEN_PBR_BIN="/opt/usr/bin/keen-pbr"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message"
}

log_info() {
    log_message info "$1"
}

emit_dnsmasq_config_entry() {
    "$KEEN_PBR_BIN" generate-resolver-config dnsmasq
    log_info "Produced dnsmasq configuration from keen-pbr lifecycle state"
}

restart_dnsmasq() {
    if [ -x /opt/etc/init.d/S56dnsmasq ]; then
        /opt/etc/init.d/S56dnsmasq restart 2>/dev/null || true
        return 0
    fi

    return 1
}

print_help() {
    cat <<EOF
Usage: $0 <command>

Commands:
  dnsmasq-config-entry   Print managed or fallback config based on daemon lifecycle state.
  activate               Restart dnsmasq; compatibility alias for reload.
  deactivate             Restart dnsmasq; compatibility alias for reload.
  restart-dnsmasq        Restart dnsmasq without changing helper-managed config.
  reload                 Restart dnsmasq; used by the system resolver hook.
  help                   Show this help text.
EOF
}

case "$1" in
    dnsmasq-config-entry)
        emit_dnsmasq_config_entry
        ;;
    activate|deactivate|restart-dnsmasq|reload)
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
