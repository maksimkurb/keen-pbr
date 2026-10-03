#!/bin/sh

DNSMASQ_CONF="${DNSMASQ_CONF:-/opt/etc/dnsmasq.conf}"
FALLBACK_CONF="${FALLBACK_CONF:-/opt/etc/keen-pbr/dnsmasq-fallback.conf}"
RESTART_CMD="${RESTART_CMD:-/opt/etc/init.d/S56dnsmasq restart}"
BEGIN_MARK="# BEGIN keen-pbr managed block"
END_MARK="# END keen-pbr managed block"
CONF_SCRIPT_LINE="conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message" || true
}

log_info() {
    log_message info "$1"
}

# Migrate dnsmasq configuration: remove keen-pbr managed blocks and conf-script lines
dnsmasq_migrate() {
    [ -f "$DNSMASQ_CONF" ] || return 0

    local changed=0
    local has_block=0
    local tmp="${DNSMASQ_CONF}.keen-pbr-migrate.tmp"
    local backup="${DNSMASQ_CONF}.keen-pbr.bak"

    # Does a complete managed block (with our conf-script line) exist?
    if awk -v begin="$BEGIN_MARK" -v end="$END_MARK" -v script="$CONF_SCRIPT_LINE" '
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
    ' "$DNSMASQ_CONF"; then
        has_block=1
    fi

    if [ "$has_block" -eq 0 ] && ! grep -qxF "$CONF_SCRIPT_LINE" "$DNSMASQ_CONF"; then
        return 0
    fi

    # Keep a backup of the original file once, before the first modification.
    if [ ! -f "$backup" ]; then
        cp "$DNSMASQ_CONF" "$backup" && log_info "Backed up $DNSMASQ_CONF to $backup"
    fi

    if [ "$has_block" -eq 0 ]; then
        # Standalone conf-script line (outside any managed block)
        grep -vxF "$CONF_SCRIPT_LINE" "$DNSMASQ_CONF" > "$tmp" || true
        cat "$tmp" > "$DNSMASQ_CONF"
        rm -f "$tmp"
        log_info "Removed standalone conf-script line from $DNSMASQ_CONF"
        changed=1
    else
        # Remove the managed block
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
        changed=1
    fi

    # Without any server= line dnsmasq would have no upstream: add a marked
    # fallback block (review it, or switch to the built-in Keenetic DNS proxy).
    if ! grep -q '^[[:space:]]*server=' "$DNSMASQ_CONF" && [ -f "$FALLBACK_CONF" ]; then
        {
            printf '\n# BEGIN keen-pbr fallback upstream (added on upgrade; replace or remove)\n'
            grep '^[[:space:]]*server=' "$FALLBACK_CONF" || true
            printf '# END keen-pbr fallback upstream\n'
        } >> "$DNSMASQ_CONF"
        log_info "Added fallback upstream block to $DNSMASQ_CONF"
    fi

    if [ "$changed" -eq 1 ]; then
        if eval "$RESTART_CMD" >/dev/null 2>&1; then
            log_info "Restarted dnsmasq after migration"
        else
            log_message err "Failed to restart dnsmasq after migration"
        fi
    fi

    return 0
}

dnsmasq_migrate || true
exit 0
