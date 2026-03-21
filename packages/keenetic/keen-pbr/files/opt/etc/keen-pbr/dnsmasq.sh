#!/bin/sh

configure_dnsmasq() {
    :
}

restore_dnsmasq() {
    :
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
