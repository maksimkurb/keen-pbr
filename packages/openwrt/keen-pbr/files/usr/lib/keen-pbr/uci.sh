#!/bin/ash

set -e

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_DIR="/etc/keen-pbr"
CACHE_DIR="/var/cache/keen-pbr"
FW4_INCLUDE_PATH="/usr/lib/keen-pbr/firewall.sh"

# Paths to bind-mount read-only into the dnsmasq procd jail.
JAIL_MOUNTS="$KEEN_PBR_BIN $CONFIG_DIR $CACHE_DIR"

[ -r /lib/functions.sh ] && . /lib/functions.sh

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message"
}

log_info() {
    log_message info "$1"
}

uci_add_list_if_new() {
    local package="$1"
    local config="$2"
    local option="$3"
    local value="$4"
    local current item

    current="$(uci -q get "${package}.${config}.${option}" || true)"
    for item in $current; do
        [ "$item" = "$value" ] && return 1
    done

    uci -q add_list "${package}.${config}.${option}=${value}"
    log_info "UCI add_list ${package}.${config}.${option}=${value}"
    return 0
}

uci_list_contains() {
    local package="$1"
    local config="$2"
    local option="$3"
    local value="$4"
    local current item

    current="$(uci -q get "${package}.${config}.${option}" || true)"
    for item in $current; do
        [ "$item" = "$value" ] && return 0
    done

    return 1
}

uci_option_exists() {
    local package="$1"
    local config="$2"
    local option="$3"

    uci -q get "${package}.${config}.${option}" >/dev/null 2>&1
}

dnsmasq_sections() {
    local section section_type

    config_load dhcp
    for section in $CONFIG_SECTIONS; do
        config_get section_type "$section" TYPE
        [ "$section_type" = "dnsmasq" ] && printf '%s\n' "$section"
    done
}

dnsmasq_confdir() {
    local section="$1"
    local confdir

    config_load dhcp
    config_get confdir "$section" confdir
    printf '%s\n' "${confdir:-/tmp/dnsmasq.${section}.d}"
}

install_mounts_for_section() {
    local section="$1"
    local path
    local changed=1

    for path in $JAIL_MOUNTS; do
        if uci_add_list_if_new dhcp "$section" addnmount "$path"; then
            changed=0
        fi
    done

    return "$changed"
}

remove_mounts_for_section() {
    local section="$1"
    local path
    local changed=1

    for path in $JAIL_MOUNTS; do
        if uci_list_contains dhcp "$section" addnmount "$path"; then
            if uci -q del_list "dhcp.${section}.addnmount=${path}"; then
                log_info "UCI del_list dhcp.${section}.addnmount=${path}"
                changed=0
            fi
        fi
    done

    return "$changed"
}

backup_list_option_for_section() {
    local section="$1"
    local option="$2"
    local backup_option="$3"
    local values value
    local changed=1

    values="$(uci -q get "dhcp.${section}.${option}" || true)"
    [ -n "$values" ] || return 1

    if uci_option_exists dhcp "$section" "$backup_option" && uci -q delete "dhcp.${section}.${backup_option}"; then
        log_info "UCI delete dhcp.${section}.${backup_option}"
        changed=0
    fi
    for value in $values; do
        if uci -q add_list "dhcp.${section}.${backup_option}=${value}"; then
            log_info "UCI add_list dhcp.${section}.${backup_option}=${value}"
            changed=0
        fi
    done
    if uci_option_exists dhcp "$section" "$option" && uci -q delete "dhcp.${section}.${option}"; then
        log_info "UCI delete dhcp.${section}.${option}"
        changed=0
    fi

    return "$changed"
}

restore_list_option_for_section() {
    local section="$1"
    local option="$2"
    local backup_option="$3"
    local values value
    local changed=1

    values="$(uci -q get "dhcp.${section}.${backup_option}" || true)"
    [ -n "$values" ] || return 1

    if uci_option_exists dhcp "$section" "$option" && uci -q delete "dhcp.${section}.${option}"; then
        log_info "UCI delete dhcp.${section}.${option}"
        changed=0
    fi
    for value in $values; do
        if uci -q add_list "dhcp.${section}.${option}=${value}"; then
            log_info "UCI add_list dhcp.${section}.${option}=${value}"
            changed=0
        fi
    done
    if uci_option_exists dhcp "$section" "$backup_option" && uci -q delete "dhcp.${section}.${backup_option}"; then
        log_info "UCI delete dhcp.${section}.${backup_option}"
        changed=0
    fi

    return "$changed"
}

commit_if_changed() {
    local package="$1"
    local changed="$2"

    [ "$changed" -eq 0 ] || return 0

    if uci -q commit "$package"; then
        log_info "Committed UCI package $package"
    fi
}

dnsmasq_install_persistent() {
    local section
    local changed=1

    for section in $(dnsmasq_sections); do
        if backup_list_option_for_section "$section" server kpbr_server; then
            changed=0
        fi
        if install_mounts_for_section "$section"; then
            changed=0
        fi
    done

    commit_if_changed dhcp "$changed"
}

dnsmasq_ensure_runtime_prereqs() {
    local section
    local changed=1

    for section in $(dnsmasq_sections); do
        if install_mounts_for_section "$section"; then
            changed=0
        fi
    done

    commit_if_changed dhcp "$changed"
}

dnsmasq_uninstall_persistent() {
    local section
    local changed=1

    for section in $(dnsmasq_sections); do
        if restore_list_option_for_section "$section" server kpbr_server; then
            changed=0
        fi
        if remove_mounts_for_section "$section"; then
            changed=0
        fi
    done

    commit_if_changed dhcp "$changed"
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
  dnsmasq-sections
  dnsmasq-confdir <section>
  dnsmasq-install-persistent
  dnsmasq-ensure-runtime-prereqs
  dnsmasq-uninstall-persistent
  firewall-sync
  firewall-remove
  help
EOF
}

case "$1" in
    dnsmasq-sections)
        dnsmasq_sections
        ;;
    dnsmasq-confdir)
        [ -n "$2" ] || exit 1
        dnsmasq_confdir "$2"
        ;;
    dnsmasq-install-persistent)
        dnsmasq_install_persistent
        ;;
    dnsmasq-ensure-runtime-prereqs)
        dnsmasq_ensure_runtime_prereqs
        ;;
    dnsmasq-uninstall-persistent)
        dnsmasq_uninstall_persistent
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
