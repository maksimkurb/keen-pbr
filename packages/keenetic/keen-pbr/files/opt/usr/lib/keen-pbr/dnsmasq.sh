#!/bin/sh

# dnsmasq conf-script entry point (dnsmasq runs it at its own start).
# dnsmasq-config-entry prints keen-pbr's upstream config while the daemon is
# running, and the fallback upstream servers otherwise (daemon stopped, config
# unreadable), so dnsmasq always has an upstream and the fallback servers are
# never used next to keen-pbr's per-list upstreams.  The daemon restarts
# dnsmasq when it becomes active, which swaps the fallback for its own config.
# All other commands are no-ops kept for old installs.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/opt/usr/bin/keen-pbr}"
FALLBACK_CONF="${FALLBACK_CONF:-/opt/etc/keen-pbr/dnsmasq-fallback.conf}"
PIDFILE="${PIDFILE:-/opt/var/run/keen-pbr.pid}"
WORK_DIR="${WORK_DIR:-/tmp/keen-pbr}"

log_info() {
    logger -s -t "keen-pbr" -p "user.info" "$1"
}

emit_fallback() {
    if [ -f "$FALLBACK_CONF" ]; then
        grep '^[[:space:]]*server=' "$FALLBACK_CONF" || true
    fi
}

# Same test as the init script's pidof: the PID file names a live process.
daemon_active() {
    [ -s "$PIDFILE" ] || return 1
    read -r pid < "$PIDFILE" || return 1
    [ -n "$pid" ] && [ -d "/proc/$pid" ]
}

# The generated config can be large: buffer it in tmpfs so a failed run
# (the binary reports errors as a comment line) can be replaced by the fallback.
emit_config_entry() {
    if ! daemon_active; then
        log_info "keen-pbr is not running; using fallback upstream servers for dnsmasq"
        emit_fallback
        return 0
    fi
    out="$WORK_DIR/dnsmasq-config-entry.$$"
    mkdir -p "$WORK_DIR" 2>/dev/null || true
    if "$KEEN_PBR_BIN" generate-resolver-config dnsmasq > "$out" 2>/dev/null &&
       [ -s "$out" ] &&
       ! grep -q '^# keen-pbr: generate-resolver-config failed' "$out"; then
        cat "$out"
    else
        log_info "keen-pbr config for dnsmasq is unavailable; using fallback upstream servers"
        emit_fallback
    fi
    rm -f "$out"
}

case "$1" in
    dnsmasq-config-entry)
        emit_config_entry
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
  dnsmasq-config-entry   Print keen-pbr's dnsmasq config while the daemon runs,
                         the fallback servers from dnsmasq-fallback.conf otherwise
  activate               No-op (dnsmasq integration removed)
  deactivate             No-op (dnsmasq integration removed)
  reload                 No-op (dnsmasq integration removed)
  restart-dnsmasq        No-op (dnsmasq integration removed)
  installed              No-op (dnsmasq integration removed)
  install-defaults       No-op (dnsmasq integration removed)
  prepare                No-op (dnsmasq integration removed)
  uninstall-persistent   No-op (dnsmasq integration removed)
  help                   Show this help text

Use migrate-dnsmasq.sh to clean up the old dnsmasq configuration on upgrade.
EOF
        ;;
    *)
        cat >&2 <<'EOF'
Unknown command. Use 'help' for usage information.
EOF
        exit 1
        ;;
esac
