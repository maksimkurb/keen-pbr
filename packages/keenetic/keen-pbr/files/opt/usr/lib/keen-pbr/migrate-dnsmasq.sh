#!/bin/sh

DNSMASQ_CONF="${DNSMASQ_CONF:-/opt/etc/dnsmasq.conf}"
FALLBACK_CONF="${FALLBACK_CONF:-/opt/etc/keen-pbr/dnsmasq-fallback.conf}"
RESTART_CMD="${RESTART_CMD:-/opt/etc/init.d/S56dnsmasq restart}"
BEGIN_MARK="# BEGIN keen-pbr managed block"
END_MARK="# END keen-pbr managed block"
CONF_SCRIPT_LINE="conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry"
FALLBACK_BEGIN_MARK="# BEGIN keen-pbr fallback upstream (added on upgrade; replace or remove)"
FALLBACK_END_MARK="# END keen-pbr fallback upstream"
# Present while keen-pbr's dnsmasq integration is installed (dnsmasq-hook.sh).
# Its conf-script supplies the upstreams (the fallback servers while the daemon
# is not running), so dnsmasq.conf needs none of its own.
INTEGRATION_BEGIN_MARK="# BEGIN keen-pbr upstream dns"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message" || true
}

log_info() {
    log_message info "$1"
}

# Keep a backup of the original file once, before the first modification.
backup_once() {
    local backup="${DNSMASQ_CONF}.keen-pbr.bak"

    if [ ! -f "$backup" ]; then
        cp "$DNSMASQ_CONF" "$backup" && log_info "Backed up $DNSMASQ_CONF to $backup"
    fi
}

# Earlier upgrades appended a permanent fallback upstream block.  The fallback
# servers are now supplied by dnsmasq.sh only while keen-pbr is not running; a
# permanent block would let queries bypass keen-pbr's per-list upstreams.  The
# block is removed when it is exactly what an upgrade wrote (marker lines and
# only server= lines found in $FALLBACK_CONF); anything else is the user's.
#
# Only when keen-pbr's managed block was just removed, or such a block is
# present, and nothing else gives dnsmasq an upstream (server=, resolv-file=,
# the integration block) the block is kept or written instead, marked and
# logged: dnsmasq-hook.sh apply drops it once the integration takes over.
# Sets fallback_changed=1 when dnsmasq.conf was modified.
fallback_block_migrate() {
    local removed_managed="$1"
    local tmp="${DNSMASQ_CONF}.keen-pbr-migrate.tmp"
    local allowed="${DNSMASQ_CONF}.keen-pbr-allowed.tmp"
    local rc=0 servers=""

    fallback_changed=0
    if [ -f "$FALLBACK_CONF" ]; then
        servers=$(grep '^[[:space:]]*server=' "$FALLBACK_CONF" || true)
    fi
    printf '%s\n' "$servers" > "$allowed"

    awk -v begin="$FALLBACK_BEGIN_MARK" -v end="$FALLBACK_END_MARK" '
        NR == FNR { allowed[$0] = 1; next }
        $0 == begin {
            if (inside) { printf "%s", buffered; kept = 1 }
            inside = 1; keep = 0; buffered = $0 ORS; next
        }
        inside {
            buffered = buffered $0 ORS
            if ($0 ~ /^# BEGIN /) keep = 1
            else if ($0 != end && !($0 in allowed)) keep = 1
            if ($0 == end) {
                if (keep) { printf "%s", buffered; kept = 1 }
                else removed = 1
                inside = 0; buffered = ""
            }
            next
        }
        { print }
        END {
            if (inside) { printf "%s", buffered; kept = 1 }
            exit(removed ? 10 : (kept ? 11 : 0))
        }
    ' "$allowed" "$DNSMASQ_CONF" > "$tmp" || rc=$?
    rm -f "$allowed"
    case "$rc" in
        0|10|11) ;;
        *) rm -f "$tmp"; return 1 ;;
    esac
    if [ "$rc" -eq 11 ]; then
        log_info "Keeping the keen-pbr fallback upstream block in $DNSMASQ_CONF: it was modified"
    fi

    if [ "$rc" -ne 10 ] && [ "$removed_managed" -eq 0 ]; then
        rm -f "$tmp"
        return 0
    fi

    if [ -z "$servers" ] ||
       grep -q '^[[:space:]]*\(server\|resolv-file\)=' "$tmp" ||
       grep -qxF "$INTEGRATION_BEGIN_MARK" "$tmp"; then
        # dnsmasq has an upstream source without the block.
        if [ "$rc" -eq 10 ]; then
            backup_once
            cat "$tmp" > "$DNSMASQ_CONF"
            log_info "Removed the keen-pbr fallback upstream block from $DNSMASQ_CONF"
            fallback_changed=1
        fi
        rm -f "$tmp"
        return 0
    fi

    # No other upstream source.
    if [ "$rc" -ne 0 ]; then
        log_info "Keeping the keen-pbr fallback upstream block in $DNSMASQ_CONF: dnsmasq has no other upstream"
        rm -f "$tmp"
        return 0
    fi
    backup_once
    {
        cat "$tmp"
        printf '\n%s\n%s\n%s\n' "$FALLBACK_BEGIN_MARK" "$servers" "$FALLBACK_END_MARK"
    } > "$tmp.new"
    cat "$tmp.new" > "$DNSMASQ_CONF"
    rm -f "$tmp" "$tmp.new"
    log_info "Added a fallback upstream block to $DNSMASQ_CONF: dnsmasq has no other upstream (replace or remove it)"
    fallback_changed=1
    return 0
}

# Migrate dnsmasq configuration: remove keen-pbr managed blocks, conf-script
# lines and fallback upstream blocks added by earlier upgrades
dnsmasq_migrate() {
    [ -f "$DNSMASQ_CONF" ] || return 0

    local changed=0
    local has_block=0
    local has_line=0
    local fallback_changed=0
    local tmp="${DNSMASQ_CONF}.keen-pbr-migrate.tmp"

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
    if grep -qxF "$CONF_SCRIPT_LINE" "$DNSMASQ_CONF"; then
        has_line=1
    fi

    if [ "$has_block" -eq 1 ] || [ "$has_line" -eq 1 ]; then
        backup_once
    fi

    if [ "$has_block" -eq 1 ]; then
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
    elif [ "$has_line" -eq 1 ]; then
        # Standalone conf-script line (outside any managed block)
        grep -vxF "$CONF_SCRIPT_LINE" "$DNSMASQ_CONF" > "$tmp" || true
        cat "$tmp" > "$DNSMASQ_CONF"
        rm -f "$tmp"
        log_info "Removed standalone conf-script line from $DNSMASQ_CONF"
        changed=1
    fi

    removed_managed=0
    [ "$changed" -eq 1 ] && removed_managed=1
    fallback_block_migrate "$removed_managed" || true
    [ "$fallback_changed" -eq 1 ] && changed=1

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
