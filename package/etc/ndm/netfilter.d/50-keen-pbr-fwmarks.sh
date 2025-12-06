#!/opt/bin/sh

#[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" -a "$table" != "nat" ] && exit 0

. /opt/etc/keen-pbr/defaults

PID=`pidof $KEEN_PBR`

# Send SIGUSR2 signal to keen-pbr service to update iptables
if [ -n "$PID" ]; then
  logger -t "keen-pbr" "Refreshing iptables"
  kill -SIGUSR2 "$PID"

  # If you want to add some additional commands after routing is applied, you can add them
  # to the /opt/etc/keen-pbr/hook.sh script
  if [ -f "/opt/etc/keen-pbr/hook.sh" ]; then
    keen_pbr_hook="netfilter"
    . /opt/etc/keen-pbr/hook.sh
  fi
fi

exit 0
