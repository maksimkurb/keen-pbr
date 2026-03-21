#!/bin/sh

KEEN_PBR_BIN="/opt/usr/bin/keen-pbr"
CONFIG_PATH="/opt/etc/keen-pbr/config.json"

resolver_type() {
    if command -v nft >/dev/null 2>&1; then
        echo "dnsmasq-nftset"
    else
        echo "dnsmasq-ipset"
    fi
}

conf_dir() {
    for dir in /opt/etc/dnsmasq.d /etc/dnsmasq.d /tmp/dnsmasq.d; do
        if [ -d "$dir" ]; then
            echo "$dir"
            return 0
        fi
    done

    echo "/opt/etc/dnsmasq.d"
}

snippet_path() {
    printf "%s/keen-pbr.conf\n" "$(conf_dir)"
}

configure_dnsmasq() {
    local dir file

    dir="$(conf_dir)"
    file="$(snippet_path)"
    mkdir -p "$dir"

    cat >"$file" <<EOF
# Managed by keen-pbr. Removed automatically on service stop.
conf-script=${KEEN_PBR_BIN} --config ${CONFIG_PATH} generate-resolver-config $(resolver_type)
EOF
}

restore_dnsmasq() {
    rm -f "$(snippet_path)"
}

reload_dnsmasq() {
    if [ -x /opt/etc/init.d/S56dnsmasq ]; then
        /opt/etc/init.d/S56dnsmasq reload 2>/dev/null || /opt/etc/init.d/S56dnsmasq restart 2>/dev/null || true
        return 0
    fi

    if command -v service >/dev/null 2>&1; then
        service dnsmasq reload 2>/dev/null || service dnsmasq restart 2>/dev/null || true
        return 0
    fi

    killall -HUP dnsmasq 2>/dev/null || true
}

case "$1" in
    configure)
        configure_dnsmasq
        ;;
    restore)
        restore_dnsmasq
        ;;
    reload)
        reload_dnsmasq
        ;;
    *)
        echo "Usage: $0 {configure|restore|reload}" >&2
        exit 1
        ;;
esac
