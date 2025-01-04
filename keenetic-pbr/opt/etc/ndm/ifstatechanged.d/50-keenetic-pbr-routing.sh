#!/opt/bin/sh

. /opt/etc/keenetic-pbr/defaults

# Check if invoked with "hook"
[ "$1" == "hook" ] || exit 0

$KEENETIC_PBR -config "$CONFIG" apply -only-routing-for-interface "$system_name"