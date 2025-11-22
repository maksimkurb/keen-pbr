#!/opt/bin/sh

# Check if invoked with "hook"
[ "$1" = "hook" ] || exit 0

. /opt/etc/keen-pbr/defaults

PIDFILE="/opt/var/run/keen-pbr.pid"

# Send SIGUSR1 signal to keen-pbr service to recheck best interface for ipsets
if [ -f "$PIDFILE" ]; then
  PID=$(cat "$PIDFILE")
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    logger -t "keen-pbr" "Sending SIGUSR1 signal to keen-pbr service (PID: $PID) - ifstatechanged.d hook for interface $system_name"
    kill -SIGUSR1 "$PID"

    # If you want to add some additional commands after routing is applied, you can add them
    # to the /opt/etc/keen-pbr/hook.sh script
    if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
      keen_pbr_hook="ifstatechanged"
      . /opt/etc/keen-pbr/hook.sh
    fi
  else
    logger -t "keen-pbr" "keen-pbr service is not running, skipping ifstatechanged.d hook for interface $system_name"
  fi
else
  logger -t "keen-pbr" "keen-pbr service PID file not found, skipping ifstatechanged.d hook for interface $system_name"
fi

exit 0
