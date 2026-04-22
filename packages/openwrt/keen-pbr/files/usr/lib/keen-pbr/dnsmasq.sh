#!/bin/ash

set -e

KEEN_PBR_BIN="/usr/sbin/keen-pbr"
CONFIG_DIR="/etc/keen-pbr"
CONFIG_PATH="/etc/keen-pbr/config.json"
CACHE_DIR="/var/cache/keen-pbr"
DNSMASQ_FALLBACK_FILE="/etc/keen-pbr/dnsmasq-fallback.conf"
PACKAGE_NAME="keen-pbr"
CONFFILE="${PACKAGE_NAME}.conf"
UCI_HELPER="/usr/lib/keen-pbr/uci.sh"

log_message() {
    local level="$1"
    local message="$2"

    logger -s -t "keen-pbr" -p "user.${level}" "$message"
}

log_info() {
    log_message info "$1"
}

write_managed_conf() {
    local target="$1"
    local line="$2"
    local description="$3"
    local existing=""

    if [ -f "$target" ]; then
        existing="$(cat "$target")"
        if [ "$existing" = "$line" ]; then
            return 0
        fi
    fi

    printf '%s\n' "$line" > "$target"
    log_info "Created $target with $description configuration"
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

dnsmasq_confdir() {
    "$UCI_HELPER" dnsmasq-confdir "$1"
}

dnsmasq_sections() {
    "$UCI_HELPER" dnsmasq-sections
}

write_temp_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    mkdir -p "$confdir"
    write_managed_conf "${confdir}/${CONFFILE}" "$(conf_script_line)" "working"
}

write_fallback_conf_for_section() {
    local section="$1"
    local confdir

    confdir="$(dnsmasq_confdir "$section")"
    mkdir -p "$confdir"
    if [ -f "$DNSMASQ_FALLBACK_FILE" ]; then
        write_managed_conf "${confdir}/${CONFFILE}" "$(fallback_conf_line)" "fallback"
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

remove_all_temp_confs() {
    local path

    for path in /tmp/dnsmasq.*.d/"${CONFFILE}" /tmp/dnsmasq.d/"${CONFFILE}"; do
        [ -e "$path" ] || continue
        rm -f "$path"
        log_info "Removed $path"
    done
}

install_persistent() {
    local section

    for section in $(dnsmasq_sections); do
        write_fallback_conf_for_section "$section" || true
    done

    "$UCI_HELPER" dnsmasq-install-persistent
}

ensure_runtime_prereqs() {
    "$UCI_HELPER" dnsmasq-ensure-runtime-prereqs
}

activate_dnsmasq() {
    local section

    ensure_runtime_prereqs

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

    for section in $(dnsmasq_sections); do
        remove_temp_conf_for_section "$section" || true
    done

    "$UCI_HELPER" dnsmasq-uninstall-persistent

    remove_all_temp_confs || true

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
  activate               Switch dnsmasq to keen-pbr dynamic resolver config, and restart dnsmasq.
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
