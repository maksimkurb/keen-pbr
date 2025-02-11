#!/opt/bin/sh

# Check if invoked with "hook"
[ "$1" = "hook" ] || exit 0

. /opt/etc/keenetic-pbr/defaults

$KEENETIC_PBR -config "$CONFIG" apply -only-routing-for-interface "$system_name" -fail-if-nothing-to-apply
status=$?

if [ "$status" = "0" ]; then
  logger -t "keenetic-pbr" "Routing applied for interface $system_name (ifstatechanged.d hook)"

  # If you want to add some additional commands after routing is applied, you can add them
  # to the /opt/etc/keenetic-pbr/hook.sh script
  if [ -f "/opt/etc/keenetic-pbr/hook.sh" ]; then
    keenetic_pbr_hook="ifstatechanged"
    . /opt/etc/keenetic-pbr/hook.sh
  fi
fi

exit 0