#!/opt/bin/sh

# Check if invoked with "hook"
[ "$1" = "hook" ] || exit 0

. /opt/etc/keen-pbr/defaults

PID=`pidof $KEEN_PBR`

# Only proceed if keen-pbr is running
if [ -z "$PID" ]; then
  exit 0
fi

STATE="$link-$connected-$up-$change"

# Function to check if this is a terminal state (final state in a sequence)
# Terminal states are those that represent a stable, final configuration:
# - up-yes-up-connected: Interface is fully connected
# - down-no-down-config: Interface is fully disconnected (disabled in UI)
# - down-no-up-config: Interface is down (server unreachable or connection failed)
is_terminal_state() {
  case "$STATE" in
    # Fully connected state
    up-yes-up-connected)
      return 0
      ;;
    # Fully disconnected state (disabled in UI)
    down-no-down-config)
      return 0
      ;;
    # Connection failed / server down
    down-no-up-config)
      return 0
      ;;
    # All other states are intermediate
    *)
      return 1
      ;;
  esac
}

# Check if this is a terminal state
if is_terminal_state; then
  # This is a final state, trigger SIGUSR1
  logger -t "keen-pbr" "Refreshing ip routes and ip rules (iface='$system_name', state='$STATE')"
  kill -SIGUSR1 "$PID"

  # If you want to add some additional commands after routing is applied, you can add them
  # to the /opt/etc/keen-pbr/hook.sh script
  if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
    keen_pbr_hook="ifstatechanged"
    . /opt/etc/keen-pbr/hook.sh
  fi
fi

exit 0
