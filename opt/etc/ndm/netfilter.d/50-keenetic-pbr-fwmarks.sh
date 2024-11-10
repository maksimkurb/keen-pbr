#!/opt/bin/sh

[ "$type" == "ip6tables" ] && exit 0
[ "$table" != "mangle" ] && exit 0

# This array would contain tuples (ipset, iface_name, fwmark, table, priority) in the following format:
#    direct pppoe1 1001 1001 1001
#    vpn nwg1 1002 1002 1002
#    vpn-2 nwg2 1003 1003 1003
IPSET_IFACE_FWMARK_PRIORITY1=(
  "direct pppoe1 1001 1001 1001"
  "vpn nwg1 1002 1002 1002"
)

# Function to add PREROUTING rules
add_prerouting_rule() {
  local ipset_name="$1"
  local fwmark="$2"

  # Check if the rule already exists
  if [ -z "$(iptables-save | grep "$ipset_name")" ]; then
    iptables -w -A PREROUTING -t mangle -m set --match-set "$ipset_name" dst,src -j MARK --set-mark "$fwmark"
  fi
}

# Iterate over IPSET_IFACE_FWMARK_PRIORITY and apply rules
for rule in "${IPSET_IFACE_FWMARK_PRIORITY[@]}"; do
  IFS=" " read -r ipset_name iface_name fwmark table priority <<< "$rule"
  add_prerouting_rule "$ipset_name" "$fwmark"
done

exit 0