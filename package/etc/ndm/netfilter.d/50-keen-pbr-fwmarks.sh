#!/opt/bin/sh

#[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" ] && exit 0

. /opt/etc/keen-pbr/defaults

PID=`pidof $KEEN_PBR`

# Send SIGUSR1 signal to keen-pbr service to recheck iptables, ip routes, and ip rules
if [ -n "$PID" ]; then
  kill -SIGUSR1 "$PID"

  # If you want to add some additional commands after routing is applied, you can add them
  # to the /opt/etc/keen-pbr/hook.sh script
  if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
    keen_pbr_hook="netfilter"
    . /opt/etc/keen-pbr/hook.sh
  fi
fi

exit 0
