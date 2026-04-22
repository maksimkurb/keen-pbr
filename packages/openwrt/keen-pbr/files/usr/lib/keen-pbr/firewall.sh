#!/bin/sh

action="${ACTION:-${1:-}}"

logger -t "keen-pbr" "[firewall.sh] Received action '$action'"

case "$action" in
    ""|includes|reload|restart)
        ;;
    *)
        exit 0
        ;;
esac

if [ -x /etc/init.d/keen-pbr ] && /etc/init.d/keen-pbr enabled; then
    logger -t "keen-pbr" "[firewall.sh] Sending SIGUSR1 to keen-pbr due to firewall action: ${action:-fw4-include}"
    /etc/init.d/keen-pbr reapply_firewall
fi
