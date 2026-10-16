#!/bin/sh

# dnsmasq conf-script entry point (dnsmasq runs it at its own start).
# dnsmasq-config-entry runs `keen-pbr generate-resolver-config dnsmasq`, which
# reads config.json and prints keen-pbr's config, or the servers of
# dnsmasq-fallback.conf while management is off.  If the binary is missing or
# fails, the fallback servers are printed here, so dnsmasq always gets an answer.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/usr/sbin/keen-pbr}"
FALLBACK_CONF="${FALLBACK_CONF:-/etc/keen-pbr/dnsmasq-fallback.conf}"

log_info() {
    logger -s -t "keen-pbr" -p "user.info" "$1"
}

emit_fallback() {
    if [ -f "$FALLBACK_CONF" ]; then
        grep '^[[:space:]]*server=' "$FALLBACK_CONF" || true
    fi
}

emit_config_entry() {
    if [ -x "$KEEN_PBR_BIN" ] && "$KEEN_PBR_BIN" generate-resolver-config dnsmasq; then
        return 0
    fi
    log_info "keen-pbr did not produce a dnsmasq config; using the fallback upstream servers"
    emit_fallback
}

case "${1:-}" in
    dnsmasq-config-entry)
        emit_config_entry
        ;;
    help|-h|--help)
        cat <<EOT
Usage: $0 dnsmasq-config-entry

Prints keen-pbr's dnsmasq config, or the servers from dnsmasq-fallback.conf
while keen-pbr management is off.
EOT
        ;;
    *)
        echo "Unknown command. Use 'help' for usage information." >&2
        exit 1
        ;;
esac
