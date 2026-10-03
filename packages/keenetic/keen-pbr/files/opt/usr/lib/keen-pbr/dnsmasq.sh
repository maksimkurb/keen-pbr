#!/bin/sh

set -e

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/opt/usr/bin/keen-pbr}"
DNSMASQ_CONF="${DNSMASQ_CONF:-/opt/etc/dnsmasq.conf}"
DNSMASQ_TEMPLATE="${DNSMASQ_TEMPLATE:-/opt/usr/lib/keen-pbr/dnsmasq.conf.template}"
DNSMASQ_BACKUP="${DNSMASQ_BACKUP:-/opt/etc/dnsmasq.conf.backup-pre-keen-pbr}"
DNSMASQ_INIT="${DNSMASQ_INIT:-/opt/etc/init.d/S56dnsmasq}"
BEGIN_MARK="# BEGIN keen-pbr managed block"
NOTICE_LINE="# Do not remove the following line, it is required for keen-pbr to function properly."
CONF_SCRIPT_LINE="conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry"
END_MARK="# END keen-pbr managed block"

# The dnsmasq integration is optional (dns.resolver_integration = "dnsmasq").
# The daemon calls `activate` before its first reload and `deactivate` when the
# integration is switched off; nothing in this package touches dnsmasq's
# configuration otherwise.

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
    if [ -x "$DNSMASQ_INIT" ]; then
        "$DNSMASQ_INIT" restart 2>/dev/null
        return $?
    fi

    return 1
}

has_managed_block() {
    [ -f "$1" ] || return 1
    awk -v begin="$BEGIN_MARK" -v end="$END_MARK" -v script="$CONF_SCRIPT_LINE" '
        $0 == begin {
            if (inside) { invalid = 1 }
            else { inside = 1; has_script = 0; invalid = 0 }
            next
        }
        inside && $0 ~ /^# BEGIN / { invalid = 1; next }
        inside && $0 == script { has_script = 1; next }
        inside && $0 == end {
            if (has_script && !invalid) found = 1
            inside = 0
            has_script = 0
            invalid = 0
            next
        }
        inside { next }
        END { exit(found ? 0 : 1) }
    ' "$1"
}

contains_conf_script() {
    [ -f "$1" ] && grep -qxF "$CONF_SCRIPT_LINE" "$1"
}

install_managed_block() {
    if has_managed_block "$DNSMASQ_CONF"; then
        return 0
    fi
    if contains_conf_script "$DNSMASQ_CONF"; then
        log_message err "Refusing to overwrite an unowned dnsmasq conf-script entry"
        return 1
    fi

    mkdir -p "$(dirname "$DNSMASQ_CONF")"
    {
        # Keep an existing file untouched apart from the appended block.
        [ ! -s "$DNSMASQ_CONF" ] || printf '\n'
        printf '%s\n%s\n%s\n%s\n' \
            "$BEGIN_MARK" "$NOTICE_LINE" "$CONF_SCRIPT_LINE" "$END_MARK"
    } >> "$DNSMASQ_CONF"
    log_info "Installed keen-pbr managed block into $DNSMASQ_CONF"
}

remove_managed_block() {
    has_managed_block "$DNSMASQ_CONF" || {
        if contains_conf_script "$DNSMASQ_CONF"; then
            log_message err "Refusing to remove an unowned dnsmasq conf-script entry"
            return 1
        fi
        return 0
    }

    local tmp="${DNSMASQ_CONF}.keen-pbr.$$"
    awk -v begin="$BEGIN_MARK" -v end="$END_MARK" -v script="$CONF_SCRIPT_LINE" '
        $0 == begin {
            if (inside) { invalid = 1; buffered = buffered $0 ORS; next }
            inside = 1; invalid = 0; has_script = 0; buffered = $0 ORS; next
        }
        inside {
            buffered = buffered $0 ORS
            if ($0 ~ /^# BEGIN /) invalid = 1
            if ($0 == begin) invalid = 1
            if ($0 == script) has_script = 1
            if ($0 == end) {
                if (!(has_script && !invalid)) printf "%s", buffered
                inside = 0
                buffered = ""
                has_script = 0
                invalid = 0
            }
            next
        }
        { print }
        END { if (inside) printf "%s", buffered }
    ' "$DNSMASQ_CONF" > "$tmp"
    cat "$tmp" > "$DNSMASQ_CONF"
    rm -f "$tmp"
    log_info "Removed keen-pbr managed block from $DNSMASQ_CONF"
}

activate_dnsmasq() {
    install_managed_block
    restart_dnsmasq
}

deactivate_dnsmasq() {
    has_managed_block "$DNSMASQ_CONF" || {
        if contains_conf_script "$DNSMASQ_CONF"; then
            log_message err "Refusing to remove unowned dnsmasq configuration"
            return 1
        fi
        return 0
    }
    local backup="${DNSMASQ_CONF}.keen-pbr-backup.$$"
    cp -p "$DNSMASQ_CONF" "$backup"
    remove_managed_block
    if restart_dnsmasq; then
        rm -f "$backup"
        return 0
    fi
    cp -p "$backup" "$DNSMASQ_CONF"
    rm -f "$backup"
    log_message err "Failed to restart dnsmasq; restored keen-pbr integration"
    return 1
}

install_defaults() {
    if [ ! -f "$DNSMASQ_TEMPLATE" ]; then
        echo "Template $DNSMASQ_TEMPLATE not found" >&2
        return 1
    fi

    if [ -f "$DNSMASQ_CONF" ]; then
        printf "Backing up existing %s to %s\n" "$DNSMASQ_CONF" "$DNSMASQ_BACKUP"
        cp "$DNSMASQ_CONF" "$DNSMASQ_BACKUP"
    fi
    printf "Installing recommended keen-pbr dnsmasq base config to %s\n" "$DNSMASQ_CONF"
    cp "$DNSMASQ_TEMPLATE" "$DNSMASQ_CONF"
}

print_help() {
    cat <<EOF
Usage: $0 <command>

Commands:
  dnsmasq-config-entry   Print managed or fallback config based on daemon lifecycle state.
  activate               Install the keen-pbr conf-script block into $DNSMASQ_CONF and restart dnsmasq.
  deactivate             Remove the keen-pbr conf-script block and restart dnsmasq.
  reload                 Restart dnsmasq; used by the system resolver hook.
  restart-dnsmasq        Restart dnsmasq without changing helper-managed config.
  installed              Exit 0 when the conf-script block is installed.
  install-defaults       Replace $DNSMASQ_CONF with the recommended keen-pbr defaults (backs up the old file).
  help                   Show this help text.
EOF
}

case "$1" in
    dnsmasq-config-entry)
        emit_dnsmasq_config_entry
        ;;
    activate)
        activate_dnsmasq
        ;;
    deactivate)
        deactivate_dnsmasq
        ;;
    restart-dnsmasq|reload)
        has_managed_block "$DNSMASQ_CONF" || {
            log_message err "Cannot reload dnsmasq: keen-pbr hook is not installed"
            exit 1
        }
        restart_dnsmasq
        ;;
    installed)
        has_managed_block "$DNSMASQ_CONF"
        ;;
    install-defaults)
        install_defaults
        ;;
    help|-h|--help)
        print_help
        ;;
    *)
        print_help >&2
        exit 1
        ;;
esac
