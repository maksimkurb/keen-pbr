#!/bin/ash

set -e

FW4_INCLUDE_PATH="/usr/lib/keen-pbr/firewall.sh"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message" || true
}

log_info() {
    log_message info "$1"
}

commit_if_changed() {
    local package="$1"
    local changed="$2"

    [ "$changed" -eq 0 ] || return 0

    if uci -q commit "$package"; then
        log_info "Committed UCI package $package"
    fi
}

is_fw4() {
    command -v fw4 >/dev/null 2>&1 || [ -x /sbin/fw4 ]
}

find_fw4_include_section() {
    local section

    for section in $(uci -q show firewall | sed -n "s/^firewall\\.\\([^.=]*\\)=include$/\\1/p"); do
        [ "$(uci -q get "firewall.${section}.path")" = "$FW4_INCLUDE_PATH" ] || continue
        printf '%s\n' "$section"
        return 0
    done

    return 1
}

set_uci_option_if_needed() {
    local key="$1"
    local value="$2"
    local current

    current="$(uci -q get "$key" || true)"
    [ "$current" = "$value" ] && return 1

    uci -q set "${key}=${value}"
    return 0
}

firewall_sync() {
    local section changed

    command -v uci >/dev/null 2>&1 || return 0
    is_fw4 || return 0

    section="$(find_fw4_include_section || true)"
    changed=1

    if [ -z "$section" ]; then
        section="$(uci -q add firewall include)" || return 1
        changed=0
    fi

    set_uci_option_if_needed "firewall.${section}.enabled" "1" && changed=0
    set_uci_option_if_needed "firewall.${section}.type" "script" && changed=0
    set_uci_option_if_needed "firewall.${section}.path" "$FW4_INCLUDE_PATH" && changed=0

    commit_if_changed firewall "$changed"
}

firewall_remove() {
    local section

    command -v uci >/dev/null 2>&1 || return 0
    is_fw4 || return 0

    section="$(find_fw4_include_section || true)"
    [ -n "$section" ] || return 0

    uci -q delete "firewall.${section}" || return 1
    commit_if_changed firewall 0
}

print_help() {
    cat <<EOF
Usage: $0 <command>

Commands:
  firewall-sync
  firewall-remove
  help
EOF
}

case "$1" in
    firewall-sync)
        firewall_sync
        ;;
    firewall-remove)
        firewall_remove
        ;;
    help|-h|--help)
        print_help
        ;;
    *)
        print_help >&2
        exit 1
        ;;
esac
