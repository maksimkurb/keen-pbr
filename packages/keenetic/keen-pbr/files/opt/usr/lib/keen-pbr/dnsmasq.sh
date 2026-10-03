#!/bin/sh

# Stub for the removed dnsmasq integration.
# The daemon may still try to call this until it is restarted or the config is updated.
# For dnsmasq-config-entry, print the fallback upstream servers to allow dnsmasq to start.
# For all other commands, exit silently (they are no longer needed).

FALLBACK_CONF="${FALLBACK_CONF:-/opt/etc/keen-pbr/dnsmasq-fallback.conf}"

log_info() {
    logger -s -t "keen-pbr" -p "user.info" "$1"
}

case "$1" in
    dnsmasq-config-entry)
        # Print fallback server lines if the file exists
        if [ -f "$FALLBACK_CONF" ]; then
            grep '^[[:space:]]*server=' "$FALLBACK_CONF" || true
        fi
        ;;
    activate|deactivate|reload|restart-dnsmasq|installed|install-defaults|prepare|uninstall-persistent)
        # Silently ignore: keen-pbr no longer manages dnsmasq
        log_info "dnsmasq integration has been removed; ignoring request: $1"
        exit 0
        ;;
    help|-h|--help)
        cat <<'EOF'
Usage: $0 <command>

This is a stub for the removed dnsmasq integration.
Supported commands (for backward compatibility):
  dnsmasq-config-entry   Print fallback servers from dnsmasq-fallback.conf
  activate               No-op (dnsmasq integration removed)
  deactivate             No-op (dnsmasq integration removed)
  reload                 No-op (dnsmasq integration removed)
  restart-dnsmasq        No-op (dnsmasq integration removed)
  installed              No-op (dnsmasq integration removed)
  install-defaults       No-op (dnsmasq integration removed)
  prepare                No-op (dnsmasq integration removed)
  uninstall-persistent   No-op (dnsmasq integration removed)
  help                   Show this help text

Note: keen-pbr no longer manages dnsmasq. DNS-driven dynamic sets are filled
by the daemon's own DNS/L7 interception. Use migrate-dnsmasq.sh to clean up
the dnsmasq configuration on upgrade.
EOF
        ;;
    *)
        cat >&2 <<'EOF'
Unknown command. Use 'help' for usage information.
EOF
        exit 1
        ;;
esac
