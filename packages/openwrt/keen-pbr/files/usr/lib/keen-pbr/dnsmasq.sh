#!/bin/ash

set -e

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_DIR="/etc/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
CACHE_DIR="/var/cache/keen-pbr"
DNSMASQ_FALLBACK_FILE="/etc/keen-pbr/dnsmasq-fallback.conf"
PACKAGE_NAME="keen-pbr"
CONFFILE="${PACKAGE_NAME}.conf"

# Paths to bind-mount read-only into the dnsmasq procd jail
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

resolver_type() {
    if command -v nft >/dev/null 2>&1; then
        echo "dnsmasq-nftset"
    else
        echo "dnsmasq-ipset"
    fi
}

conf_script_line() {
    printf 'conf-script=%s --config %s generate-resolver-config %s' \
        "$KEEN_PBR_BIN" "$CONFIG_PATH" "$(resolver_type)"
}

fallback_conf_line() {
    printf 'conf-file=%s' "$DNSMASQ_FALLBACK_FILE"
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
    config_get confdir "$section" confdir
    printf '%s' "${confdir:-/tmp/dnsmasq.${section}.d}"
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
    local copied=1

    values="$(uci -q get "dhcp.${section}.${option}" || true)"
    [ -n "$values" ] || return 1

    if uci_option_exists dhcp "$section" "$backup_option" && uci -q delete "dhcp.${section}.${backup_option}"; then
        log_info "UCI delete dhcp.${section}.${backup_option}"
        copied=0
    fi
    for value in $values; do
        if uci -q add_list "dhcp.${section}.${backup_option}=${value}"; then
            log_info "UCI add_list dhcp.${section}.${backup_option}=${value}"
            copied=0
        fi
    done
    if uci_option_exists dhcp "$section" "$option" && uci -q delete "dhcp.${section}.${option}"; then
        log_info "UCI delete dhcp.${section}.${option}"
        copied=0
    fi

    return "$copied"
}

restore_list_option_for_section() {
    local section="$1"
    local option="$2"
    local backup_option="$3"
    local values value
    local restored=1

    values="$(uci -q get "dhcp.${section}.${backup_option}" || true)"
    [ -n "$values" ] || return 1

    if uci_option_exists dhcp "$section" "$option" && uci -q delete "dhcp.${section}.${option}"; then
        log_info "UCI delete dhcp.${section}.${option}"
        restored=0
    fi
    for value in $values; do
        if uci -q add_list "dhcp.${section}.${option}=${value}"; then
            log_info "UCI add_list dhcp.${section}.${option}=${value}"
            restored=0
        fi
    done
    if uci_option_exists dhcp "$section" "$backup_option" && uci -q delete "dhcp.${section}.${backup_option}"; then
        log_info "UCI delete dhcp.${section}.${backup_option}"
        restored=0
    fi

    return "$restored"
}

write_temp_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    mkdir -p "$confdir"
    printf '%s\n' "$(conf_script_line)" > "${confdir}/${CONFFILE}"
    log_info "Created ${confdir}/${CONFFILE}"
}

write_fallback_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    mkdir -p "$confdir"
    if [ -f "$DNSMASQ_FALLBACK_FILE" ]; then
        printf '%s\n' "$(fallback_conf_line)" > "${confdir}/${CONFFILE}"
        log_info "Created ${confdir}/${CONFFILE}"
    else
        rm -f "${confdir}/${CONFFILE}"
        log_info "Removed ${confdir}/${CONFFILE}"
    fi
}

remove_temp_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    rm -f "${confdir}/${CONFFILE}"
    log_info "Removed ${confdir}/${CONFFILE}"
}

install_persistent() {
    local section
    local changed=1

    for section in $(dnsmasq_sections); do
        write_fallback_conf_for_section "$section" || true
        if backup_list_option_for_section "$section" server kpbr_server; then
            changed=0
        fi
        if install_mounts_for_section "$section"; then
            changed=0
        fi
    done

    if [ "$changed" -eq 0 ]; then
        if uci -q commit dhcp; then
            log_info "Committed UCI package dhcp"
        fi
    fi
}

ensure_runtime_prereqs() {
    local section
    local changed=1

    for section in $(dnsmasq_sections); do
        if install_mounts_for_section "$section"; then
            changed=0
        fi
    done

    if [ "$changed" -eq 0 ]; then
        if uci -q commit dhcp; then
            log_info "Committed UCI package dhcp"
        fi
    fi
}

activate_dnsmasq() {
    local section

    for section in $(dnsmasq_sections); do
        write_temp_conf_for_section "$section" || true
    done

    restart_dnsmasq
}

deactivate_dnsmasq() {
    local section

    for section in $(dnsmasq_sections); do
        write_fallback_conf_for_section "$section" || true
    done

    restart_dnsmasq
}

uninstall_persistent() {
    local section
    local changed=1

    for section in $(dnsmasq_sections); do
        remove_temp_conf_for_section "$section" || true
        if restore_list_option_for_section "$section" server kpbr_server; then
            changed=0
        fi
        if remove_mounts_for_section "$section"; then
            changed=0
        fi
    done

    if [ "$changed" -eq 0 ]; then
        if uci -q commit dhcp; then
            log_info "Committed UCI package dhcp"
        fi
    fi

    restart_dnsmasq
}

restart_dnsmasq() {
    /etc/init.d/dnsmasq restart 2>/dev/null || true
}

print_help() {
    cat <<EOF
Usage: $0 <command>

Commands:
  install-persistent     Seed fallback dnsmasq config and install persistent integration.
  ensure-runtime-prereqs Ensure dnsmasq jail/runtime prerequisites are configured.
  activate               Switch dnsmasq to keen-pbr dynamic resolver config and restart dnsmasq.
  deactivate             Switch dnsmasq to fallback resolver config and restart dnsmasq.
  uninstall-persistent   Remove persistent integration and helper-managed runtime config.
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
