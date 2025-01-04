#!/opt/bin/sh

. /opt/etc/keenetic-pbr/defaults

# Check if invoked with "hook"
[ "$1" = "hook" ] || exit 0

$KEENETIC_PBR -config "$CONFIG" apply -only-routing-for-interface "$system_name" -fail-if-nothing-to-apply
status=$?

if [ "$status" = "0" ]; then
  logger -t "keenetic-pbr" "Routing applied for interface $system_name (ifstatechanged.d hook)"

  # Do your own stuff here
fi

exit 0