#!/bin/sh

set -e

KEEN_PBR_BIN="/opt/usr/bin/keen-pbr"
CONFIG_PATH="/opt/etc/keen-pbr/config.json"
DNSMASQ_CONF="/opt/etc/dnsmasq.conf"
DNSMASQ_TMP_DIR="/tmp/dnsmasq.d"
DNSMASQ_TMP_FILE="${DNSMASQ_TMP_DIR}/keen-pbr.conf"
DNSMASQ_FALLBACK_FILE="/opt/etc/keen-pbr/dnsmasq-fallback.conf"
BLOCK_START="# BEGIN keen-pbr managed block"
BLOCK_END="# END keen-pbr managed block"
BLOCK_LINE="conf-dir=/tmp/dnsmasq.d,*.conf"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message"
}

log_info() {
    log_message info "$1"
}

log_warn() {
    log_message warn "$1"
}

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

fallback_conf_line() {
    printf 'conf-file=%s\n' "$DNSMASQ_FALLBACK_FILE"
}

has_managed_block() {
    [ -f "$DNSMASQ_CONF" ] || return 1

    awk -v start="$BLOCK_START" -v line="$BLOCK_LINE" -v end="$BLOCK_END" '
        function trim(value) {
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            return value
        }

        {
            current = trim($0)
        }

        current == start {
            found = 1
            in_block = 1
            next
        }

        in_block && current == end {
            exit !line_found
        }

        in_block && current == line {
            line_found = 1
        }

        END {
            if (!found || !line_found) {
                exit 1
            }
        }
    ' "$DNSMASQ_CONF"
}

install_persistent() {
    mkdir -p "$DNSMASQ_TMP_DIR"
    if [ -f "$DNSMASQ_FALLBACK_FILE" ] && [ ! -f "$DNSMASQ_TMP_FILE" ]; then
        fallback_conf_line > "$DNSMASQ_TMP_FILE"
        log_info "Created ${DNSMASQ_TMP_FILE}"
    fi
}

ensure_runtime_prereqs() {
    install_persistent
    if ! has_managed_block; then
        log_warn "Missing keen-pbr dnsmasq include block in ${DNSMASQ_CONF}; expected block pointing to ${DNSMASQ_TMP_DIR}"
    fi
}

activate_dnsmasq() {
    mkdir -p "$DNSMASQ_TMP_DIR"
    conf_script_line > "$DNSMASQ_TMP_FILE"
    log_info "Created ${DNSMASQ_TMP_FILE}"
    restart_dnsmasq
}

deactivate_dnsmasq() {
    if [ -f "$DNSMASQ_FALLBACK_FILE" ]; then
        fallback_conf_line > "$DNSMASQ_TMP_FILE"
        log_info "Created ${DNSMASQ_TMP_FILE}"
    else
        rm -f "$DNSMASQ_TMP_FILE"
        log_info "Removed ${DNSMASQ_TMP_FILE}"
    fi
    restart_dnsmasq
}

uninstall_persistent() {
    rm -f "$DNSMASQ_TMP_FILE"
    log_info "Removed ${DNSMASQ_TMP_FILE}"
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
  install-persistent     Seed fallback dnsmasq config and install persistent integration.
  ensure-runtime-prereqs Create runtime directories and warn if dnsmasq lacks the managed conf-dir block.
  activate               Switch dnsmasq to keen-pbr dynamic resolver config and restart dnsmasq.
  deactivate             Switch dnsmasq to fallback resolver config and restart dnsmasq.
  uninstall-persistent   Remove helper-managed runtime config.
  restart-dnsmasq        Restart dnsmasq without changing helper-managed config.
  reload                 Alias for restart-dnsmasq; used by the system resolver hook.
  help                   Show this help text.
EOF
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
