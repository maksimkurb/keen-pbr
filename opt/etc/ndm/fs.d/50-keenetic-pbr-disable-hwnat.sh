#!/opt/bin/sh

if [ "$1" = "start" ] ; then
  logger -t "keenetic-pbr" "Disabling HW NAT"

  # Disable HW NAT
  sysctl -w net.ipv4.netfilter.ip_conntrack_fastnat=0 || true # NDMS2
  sysctl -w net.netfilter.nf_conntrack_fastnat=0 || true # NDMS3
fi