#!/opt/bin/sh

#[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" ] && exit 0

/opt/etc/init.d/S80keenetic-pbr apply-routing

exit 0