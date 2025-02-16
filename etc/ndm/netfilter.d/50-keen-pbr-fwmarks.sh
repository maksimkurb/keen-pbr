#!/opt/bin/sh

#[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" ] && exit 0

. /opt/etc/keen-pbr/defaults

$KEEN_PBR -config "$CONFIG" apply -skip-ipset

logger -t "keen-pbr" "Routing applied for all interfaces (netfilter.d hook)"

# If you want to add some additional commands after routing is applied, you can add them
# to the /opt/etc/keen-pbr/hook.sh script
if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
  keen_pbr_hook="netfilter"
  . /opt/etc/keen-pbr/hook.sh
fi

exit 0