#!/opt/bin/sh

# Check if invoked with "hook"
[ "$1" = "hook" ] || exit 0

# Only trigger on control layer changes
if [ "$layer" != "ctrl" ]; then
  exit 0
fi

. /opt/etc/keen-pbr/defaults

PID=`pidof $KEEN_PBR`

# Only proceed if keen-pbr is running
if [ -z "$PID" ]; then
  exit 0
fi
# Trigger SIGUSR1 to refresh routing
logger -t "keen-pbr" "Refreshing ip routes and ip rules (iface='$system_name' layer='$layer', level='$level')"
kill -SIGUSR1 "$PID"

# If you want to add some additional commands after routing is applied, you can add them
# to the /opt/etc/keen-pbr/hook.sh script
if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
  keen_pbr_hook="iflayerchanged"
  . /opt/etc/keen-pbr/hook.sh
fi

exit 0

