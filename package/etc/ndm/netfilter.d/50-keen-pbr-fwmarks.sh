#!/opt/bin/sh

#[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" ] && exit 0

. /opt/etc/keen-pbr/defaults

PIDFILE="/opt/var/run/keen-pbr.pid"

# Send HUP signal to keen-pbr service to recheck iptables, ip routes, and ip rules
if [ -f "$PIDFILE" ]; then
  PID=$(cat "$PIDFILE")
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    logger -t "keen-pbr" "Sending HUP signal to keen-pbr service (PID: $PID) - netfilter.d hook"
    kill -HUP "$PID"
  else
    logger -t "keen-pbr" "keen-pbr service is not running, skipping netfilter.d hook"
  fi
else
  logger -t "keen-pbr" "keen-pbr service PID file not found, skipping netfilter.d hook"
fi

# If you want to add some additional commands after routing is applied, you can add them
# to the /opt/etc/keen-pbr/hook.sh script
if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
  keen_pbr_hook="netfilter"
  . /opt/etc/keen-pbr/hook.sh
fi

exit 0