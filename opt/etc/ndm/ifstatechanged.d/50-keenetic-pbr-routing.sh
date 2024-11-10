#!/opt/bin/sh

# This array would contain tuples (ipset, iface_name, fwmark, table, priority) in the following format:
#    direct pppoe1 1001 1001 1001
#    vpn nwg1 1002 1002 1002
#    vpn-2 nwg2 1003 1003 1003
IPSET_IFACE_FWMARK_PRIORITY1=(
  "direct pppoe1 1001 1001 1001"
  "vpn nwg1 1002 1002 1002"
)

# Function to add routing rules
add_routing_rules() {
  local ipset_name="$1"
  local iface_name="$2"
  local fwmark="$3"
  local table="$4"
  local priority="$5"

  if [ -z "$(ip rule show | grep "$priority:")" ]; then
    logger -t "keenetic-pbr" "Adding rule to forward all packets with fwmark=$flush to table=$table (priority=$priority)"
    ip rule add fwmark "$fwmark" table "$table" priority "$priority"
  fi

  if [ -z "$(ip route list table $table)" ]; then
    logger -t "keenetic-pbr" "Adding default route interface=$iface_name to table=$table"
    ip route add default dev "$iface_name" table "$table"
  fi
}

# Function to delete routing rules
delete_routing_rules() {
  local ipset_name="$1"
  local iface_name="$2"
  local fwmark="$3"
  local table="$4"
  local priority="$5"

  if [ -n "$(ip rule show | grep "$priority:")" ]; then
    logger -t "keenetic-pbr" "Removing rule to forward all packets with fwmark=$flush to table=$table (priority=$priority)"
    ip rule del table "$table"
  fi

  if [ -n "$(ip route list table $table)" ]; then
    logger -t "keenetic-pbr" "Removing default route interface=$iface_name to table=$table"
    ip route flush table "$table"
  fi
}

# Check if invoked with "hook"
[ "$1" == "hook" ] || exit 0

# Check if $system_name matches any $iface_name in IPSET_IFACE_FWMARK_PRIORITY
for rule in "${IPSET_IFACE_FWMARK_PRIORITY[@]}"; do
  read -r ipset_name iface_name fwmark table priority <<< "$rule"

  if [ "$system_name" == "$iface_name" ]; then
    logger -t "keenetic-pbr" "Interface $system_name $change is changed: up:$up link:$link connected:$connected"

    case ${change}-${connected}-${link}-${up} in
      link-yes-up-up)
        logger -t "keenetic-pbr" "Adding routing rules for $system_name"
        add_routing_rules "$ipset_name" "$iface_name" "$fwmark" "$table" "$priority"
      ;;
      link-no-down-down)
        logger -t "keenetic-pbr" "Removing routing rules for $system_name"
        delete_routing_rules "$iface_name" "$ipset_name" "$priority"
      ;;
    esac

    # Exit after handling the matched interface to avoid further processing
    exit 0
  fi
done
