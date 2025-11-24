#!/opt/bin/sh

# Check if invoked with "hook"
[ "$1" = "hook" ] || exit 0

. /opt/etc/keen-pbr/defaults

PIDFILE="/opt/var/run/keen-pbr.pid"

# Send SIGUSR1 signal to keen-pbr service to recheck best interface for ipsets
if [ -f "$PIDFILE" ]; then
  PID=$(cat "$PIDFILE")
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    kill -SIGUSR1 "$PID"

    # If you want to add some additional commands after routing is applied, you can add them
    # to the /opt/etc/keen-pbr/hook.sh script
    if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
      keen_pbr_hook="ifstatechanged"
      . /opt/etc/keen-pbr/hook.sh
    fi
  fi
fi

exit 0
