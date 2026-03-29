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
    return 0
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
        if uci -q del_list "dhcp.${section}.addnmount=${path}"; then
            changed=0
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

    uci -q delete "dhcp.${section}.${backup_option}" || true
    for value in $values; do
        uci -q add_list "dhcp.${section}.${backup_option}=${value}"
    done
    uci -q delete "dhcp.${section}.${option}" || true
    changed=0

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

    uci -q delete "dhcp.${section}.${option}" || true
    for value in $values; do
        uci -q add_list "dhcp.${section}.${option}=${value}"
    done
    uci -q delete "dhcp.${section}.${backup_option}" || true
    changed=0

    return "$changed"
}

write_temp_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    mkdir -p "$confdir"
    printf '%s\n' "$(conf_script_line)" > "${confdir}/${CONFFILE}"
}

write_fallback_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    mkdir -p "$confdir"
    if [ -f "$DNSMASQ_FALLBACK_FILE" ]; then
        cp "$DNSMASQ_FALLBACK_FILE" "${confdir}/${CONFFILE}"
    else
        rm -f "${confdir}/${CONFFILE}"
    fi
}

remove_temp_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    rm -f "${confdir}/${CONFFILE}"
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
        uci -q commit dhcp || true
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
        uci -q commit dhcp || true
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
        uci -q commit dhcp || true
    fi
}

restart_dnsmasq() {
    /etc/init.d/dnsmasq restart 2>/dev/null || true
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
    configure)
        activate_dnsmasq
        ;;
    cleanup)
        deactivate_dnsmasq
        ;;
    restore)
        uninstall_persistent
        ;;
    reload)
        restart_dnsmasq
        ;;
    *)
        echo "Usage: $0 {install-persistent|ensure-runtime-prereqs|activate|deactivate|uninstall-persistent|restart-dnsmasq}" >&2
        exit 1
        ;;
esac
