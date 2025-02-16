#!/opt/bin/sh

# Check if invoked with "hook"
[ "$1" = "hook" ] || exit 0

. /opt/etc/keen-pbr/defaults

$KEEN_PBR -config "$CONFIG" apply -only-routing-for-interface "$system_name" -fail-if-nothing-to-apply
status=$?

if [ "$status" = "0" ]; then
  logger -t "keen-pbr" "Routing applied for interface $system_name (ifstatechanged.d hook)"

  # If you want to add some additional commands after routing is applied, you can add them
  # to the /opt/etc/keen-pbr/hook.sh script
  if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
    keen_pbr_hook="ifstatechanged"
    . /opt/etc/keen-pbr/hook.sh
  fi
fi

exit 0