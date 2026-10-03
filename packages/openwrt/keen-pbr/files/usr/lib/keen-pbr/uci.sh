#!/bin/ash

set -e

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
FW4_INCLUDE_PATH="/usr/lib/keen-pbr/firewall.sh"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message" || true
}

log_info() {
    log_message info "$1"
}

uci_option_exists() {
    local package="$1"
    local config="$2"
    local option="$3"

    uci -q get "${package}.${config}.${option}" >/dev/null 2>&1
}

dnsmasq_sections() {
    uci -q show dhcp | sed -n "s/^dhcp\\.\\([^.=]*\\)=dnsmasq$/\\1/p"
}

commit_if_changed() {
    local package="$1"
    local changed="$2"

    [ "$changed" -eq 0 ] || return 0

    if uci -q commit "$package"; then
        log_info "Committed UCI package $package"
    fi
}

DNSMASQ_RESTART_CMD="${DNSMASQ_RESTART_CMD:-/etc/init.d/dnsmasq restart}"

list_contains() {
    local needle="$1"
    local item
    shift

    for item in "$@"; do
        [ "$item" = "$needle" ] && return 0
    done

    return 1
}

# Undo the changes made by the removed dnsmasq integration: put the upstream
# servers saved in kpbr_server back into server, drop our jail mounts and the
# conf-script drop-in.  Idempotent; sections without our markers are untouched.
dnsmasq_migrate_from_keen_pbr() {
    local section
    local changed=1
    local kpbr_servers current_servers server_item mount_item
    local confdir conf_path conf_line

    command -v uci >/dev/null 2>&1 || return 0
    conf_line="conf-script=${KEEN_PBR_BIN} generate-resolver-config dnsmasq"

    for section in $(dnsmasq_sections); do
        if uci_option_exists dhcp "$section" kpbr_server; then
            kpbr_servers="$(uci -q get "dhcp.${section}.kpbr_server" || true)"
            current_servers="$(uci -q get "dhcp.${section}.server" || true)"
            for server_item in $kpbr_servers; do
                # shellcheck disable=SC2086
                if ! list_contains "$server_item" $current_servers; then
                    uci -q add_list "dhcp.${section}.server=${server_item}" || true
                    current_servers="${current_servers:+$current_servers }$server_item"
                    log_info "Restored dhcp.${section}.server=${server_item} from kpbr_server"
                fi
            done
            uci -q delete "dhcp.${section}.kpbr_server" || true
            log_info "Deleted dhcp.${section}.kpbr_server"
            changed=0
        fi

        # Remove only the jail mounts that the integration added.
        for mount_item in $(uci -q get "dhcp.${section}.addnmount" || true); do
            case "$mount_item" in
                /usr/sbin/keen-pbr|/etc/keen-pbr|/var/cache/keen-pbr|/var/run/keen-pbr)
                    uci -q del_list "dhcp.${section}.addnmount=${mount_item}" || true
                    log_info "Removed dhcp.${section}.addnmount=${mount_item}"
                    changed=0
                    ;;
            esac
        done

        # Delete the drop-in only if it holds nothing but our conf-script line.
        confdir="$(uci -q get "dhcp.${section}.confdir" || true)"
        conf_path="${confdir:-/tmp/dnsmasq.${section}.d}/keen-pbr.conf"
        if [ -f "$conf_path" ] && grep -qxF "$conf_line" "$conf_path" &&
           ! grep -vxF "$conf_line" "$conf_path" | grep -q '[^[:space:]]'; then
            rm -f "$conf_path"
            log_info "Deleted $conf_path"
            changed=0
        fi
    done

    [ "$changed" -eq 0 ] || return 0

    commit_if_changed dhcp "$changed"
    eval "$DNSMASQ_RESTART_CMD" >/dev/null 2>&1 || true
    return 0
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
  dnsmasq-migrate-from-keen-pbr
  firewall-sync
  firewall-remove
  help
EOF
}

case "$1" in
    dnsmasq-migrate-from-keen-pbr)
        dnsmasq_migrate_from_keen_pbr
        ;;
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
