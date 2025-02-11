#!/opt/bin/sh

#[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" ] && exit 0

. /opt/etc/keenetic-pbr/defaults

$KEENETIC_PBR -config "$CONFIG" apply -skip-ipset

logger -t "keenetic-pbr" "Routing applied for all interfaces (netfilter.d hook)"

# If you want to add some additional commands after routing is applied, you can add them
# to the /opt/etc/keenetic-pbr/hook.sh script
if [ -f "/opt/etc/keenetic-pbr/hook.sh" ]; then
  keenetic_pbr_hook="netfilter"
  . /opt/etc/keenetic-pbr/hook.sh
fi

exit 0