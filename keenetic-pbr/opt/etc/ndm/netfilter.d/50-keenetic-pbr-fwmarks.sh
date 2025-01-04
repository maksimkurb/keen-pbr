#!/opt/bin/sh

#[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" ] && exit 0

/opt/etc/init.d/S80keenetic-pbr apply-routing

logger -t "keenetic-pbr" "Routing applied for all interfaces (netfilter.d hook)"

# Do your own stuff here

exit 0