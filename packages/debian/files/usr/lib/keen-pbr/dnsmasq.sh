#!/bin/sh

set -eu

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/usr/sbin/keen-pbr}"
DNSMASQ_FALLBACK_FILE="${DNSMASQ_FALLBACK_FILE:-/etc/keen-pbr/dnsmasq-fallback.conf}"
DNSMASQ_CONF="${DNSMASQ_CONF:-/etc/dnsmasq.conf}"
DNSMASQ_DROPIN="${DNSMASQ_DROPIN:-/etc/dnsmasq.d/keen-pbr.conf}"
DNSMASQ_TEMPLATE="${DNSMASQ_TEMPLATE:-/usr/lib/keen-pbr/dnsmasq.conf.template}"
DNSMASQ_BACKUP="${DNSMASQ_BACKUP:-/etc/dnsmasq.conf.backup-pre-keen-pbr}"
BEGIN_MARK="# BEGIN keen-pbr managed block"
END_MARK="# END keen-pbr managed block"
CONF_SCRIPT_LINE="conf-script=/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry"
STATE_DIR="${STATE_DIR:-/tmp/keen-pbr}"
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

log_error() {
    log_message err "$1"
}

ensure_xt_multiport_loaded() {
    if command -v nft >/dev/null 2>&1; then
        return 0
    fi

    if lsmod | grep -q '^xt_multiport[[:space:]]'; then
        return 0
    fi

    if command -v modprobe >/dev/null 2>&1 && modprobe xt_multiport 2>/dev/null; then
        return 0
    fi

    module_path="$(find "/lib/modules/$(uname -r)" -type f \
        -name 'xt_multiport.ko*' 2>/dev/null | head -n 1 || true)"

    if [ -n "$module_path" ]; then
        log_error "Failed to load xt_multiport from $module_path"
    else
        log_error "xt_multiport module not loaded and not found under /lib/modules/$(uname -r)"
    fi

    return 0
}

fallback_conf_line() {
    printf 'conf-file=%s\n' "$DNSMASQ_FALLBACK_FILE"
}

active_conf_line() {
    "$KEEN_PBR_BIN" generate-resolver-config dnsmasq
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
        log_info "Produced dnsmasq keen-pbr managed config"
    else
        fallback_conf_line
        log_info "Produced dnsmasq fallback config entry"
    fi
}

# The dnsmasq integration is optional (dns.resolver_integration = "dnsmasq").
# The daemon calls `activate` before its first reload and `deactivate` when the
# integration is switched off.  The conf-script entry lives either in the
# managed block of /etc/dnsmasq.conf (recommended defaults template, existing
# installs) or in the drop-in below; it is never installed twice.
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

conf_has_managed_block() {
    has_managed_block "$DNSMASQ_CONF"
}

dropin_has_managed_block() {
    has_managed_block "$DNSMASQ_DROPIN"
}

dropin_is_exact_managed_block() {
    [ -f "$DNSMASQ_DROPIN" ] || return 1
    awk -v begin="$BEGIN_MARK" -v end="$END_MARK" -v script="$CONF_SCRIPT_LINE" '
        NR == 1 && $0 == begin { ok_begin = 1; next }
        NR == 2 && $0 == script { ok_script = 1; next }
        NR == 3 && $0 == end { ok_end = 1; next }
        { extra = 1 }
        END { exit(ok_begin && ok_script && ok_end && !extra && NR == 3 ? 0 : 1) }
    ' "$DNSMASQ_DROPIN"
}

hook_installed() {
    conf_has_managed_block || dropin_is_exact_managed_block
}

install_hook() {
    if [ -e "$DNSMASQ_DROPIN" ] && ! dropin_is_exact_managed_block; then
        log_error "Refusing to overwrite an unowned dnsmasq conf-script entry"
        return 1
    fi
    if conf_has_managed_block || dropin_is_exact_managed_block; then
        return 0
    fi
    if contains_conf_script "$DNSMASQ_CONF"; then
        log_error "Refusing to overwrite an unowned dnsmasq conf-script entry"
        return 1
    fi
    mkdir -p "$(dirname "$DNSMASQ_DROPIN")"
    (umask 022; printf '%s\n%s\n%s\n' "$BEGIN_MARK" "$CONF_SCRIPT_LINE" "$END_MARK" > "$DNSMASQ_DROPIN")
    log_info "Installed keen-pbr conf-script entry into $DNSMASQ_DROPIN"
}

remove_hook() {
    if dropin_is_exact_managed_block; then
        rm -f "$DNSMASQ_DROPIN"
    elif [ -e "$DNSMASQ_DROPIN" ]; then
        log_error "Refusing to remove unowned dnsmasq drop-in $DNSMASQ_DROPIN"
        return 1
    fi
    if conf_has_managed_block; then
        tmp="${DNSMASQ_CONF}.keen-pbr.$$"
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
    elif contains_conf_script "$DNSMASQ_CONF"; then
        log_error "Refusing to remove an unowned dnsmasq conf-script entry"
        return 1
    fi
    log_info "Removed keen-pbr conf-script entry from dnsmasq configuration"
}

activate_dnsmasq() {
    install_hook
    set_active_state "Y"
    log_info "Marked keen-pbr dnsmasq state as active"
    if restart_dnsmasq; then return 0; fi
    set_active_state "N"
    log_error "Failed to restart dnsmasq; left integration in fallback state"
    return 1
}

deactivate_dnsmasq() {
    hook_installed || {
        if contains_conf_script "$DNSMASQ_CONF" || [ -e "$DNSMASQ_DROPIN" ]; then
            log_error "Refusing to remove unowned dnsmasq configuration"
            return 1
        fi
        return 0
    }
    backup_conf=""
    backup_dropin=""
    if conf_has_managed_block; then
        backup_conf="${DNSMASQ_CONF}.keen-pbr-backup.$$"
        cp -p "$DNSMASQ_CONF" "$backup_conf"
    fi
    if dropin_is_exact_managed_block; then
        backup_dropin="${DNSMASQ_DROPIN}.keen-pbr-backup.$$"
        cp -p "$DNSMASQ_DROPIN" "$backup_dropin"
    fi
    if ! remove_hook; then
        [ -z "$backup_conf" ] || rm -f "$backup_conf"
        [ -z "$backup_dropin" ] || rm -f "$backup_dropin"
        return 1
    fi
    set_active_state "N"
    log_info "Removed keen-pbr dnsmasq integration"
    if restart_dnsmasq; then
        [ -z "$backup_conf" ] || rm -f "$backup_conf"
        [ -z "$backup_dropin" ] || rm -f "$backup_dropin"
        return 0
    fi
    [ -z "$backup_conf" ] || cp -p "$backup_conf" "$DNSMASQ_CONF"
    [ -z "$backup_dropin" ] || cp -p "$backup_dropin" "$DNSMASQ_DROPIN"
    [ -z "$backup_conf" ] || rm -f "$backup_conf"
    [ -z "$backup_dropin" ] || rm -f "$backup_dropin"
    set_active_state "Y"
    log_error "Failed to restart dnsmasq; restored keen-pbr integration"
    return 1
}

# Stop of the service: keep the integration installed but let dnsmasq fall
# back to the static config.  Does nothing when the integration is not
# installed, so the service unit can call it unconditionally.
fallback_dnsmasq() {
    hook_installed || return 0
    previous_state="N"
    if is_active; then previous_state="Y"; fi
    set_active_state "N"
    log_info "Marked keen-pbr dnsmasq state as inactive (fallback)"
    if restart_dnsmasq; then return 0; fi
    set_active_state "$previous_state"
    log_error "Failed to restart dnsmasq; restored active state"
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
    rm -f "$DNSMASQ_DROPIN"
}

restart_dnsmasq() {
    if command -v systemctl >/dev/null 2>&1; then
        systemctl restart dnsmasq >/dev/null 2>&1
    elif command -v service >/dev/null 2>&1; then
        service dnsmasq restart >/dev/null 2>&1
    else
        log_error "No service manager is available to restart dnsmasq"
        return 1
    fi
}

print_help() {
    cat <<EOF
Usage: $0 <command>

Commands:
  dnsmasq-config-entry   Print the dnsmasq config entry for the current active state.
  activate               Install the conf-script entry, mark the state active and restart dnsmasq.
  deactivate             Remove the conf-script entry and restart dnsmasq.
  fallback               Switch dnsmasq to the static fallback config (no-op if not installed).
  reload                 Switch to managed config and restart; used by the system resolver hook.
  restart-dnsmasq        Restart dnsmasq without changing helper-managed config.
  installed              Exit 0 when the conf-script entry is installed.
  ensure-modules         Load kernel modules needed by the iptables backend.
  install-defaults       Replace $DNSMASQ_CONF with the recommended keen-pbr defaults (backs up the old file).
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
    fallback)
        fallback_dnsmasq
        ;;
    restart-dnsmasq)
        restart_dnsmasq
        ;;
reload)
        hook_installed || {
            log_error "Cannot reload dnsmasq: keen-pbr hook is not installed"
            exit 1
        }
        set_active_state "Y"
        restart_dnsmasq
        ;;
    installed)
        hook_installed
        ;;
    ensure-modules)
        ensure_xt_multiport_loaded
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
