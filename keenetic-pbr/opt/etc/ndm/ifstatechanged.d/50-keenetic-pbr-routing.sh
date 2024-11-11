#!/opt/bin/sh

CONFIG="/opt/etc/keenetic-pbr/keenetic-pbr.conf"
KEENETIC_PBR="keenetic-pbr"

# Check if invoked with "hook"
[ "$1" == "hook" ] || exit 0

# Function to add routing rules
add_routing_rules() {
  local ipset_name="$1"
  local iface_name="$2"
  local fwmark="$3"
  local table="$4"
  local priority="$5"

  if [ -z "$(ip rule show | grep "$priority:")" ]; then
    logger -t "keenetic-pbr" "Adding rule to forward all packets with fwmark=$fwmark to table=$table (priority=$priority)"
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
    logger -t "keenetic-pbr" "Removing rules for table $table"
    ip rule del table "$table"
  fi

  if [ -n "$(ip route list table $table)" ]; then
    logger -t "keenetic-pbr" "Removing route table $table"
    ip route flush table "$table"
  fi
}

$KEENETIC_PBR -config "$CONFIG" gen-routing-config | while IFS=" " read -r ipset_name iface_name fwmark table priority; do
  if [ "$system_name" == "$iface_name" ]; then
    case ${connected}-${link}-${up} in
      yes-up-up)
	      logger -t "keenetic-pbr" "Interface $system_name is connected"
        logger -t "keenetic-pbr" "Adding routing rules for $system_name"
        add_routing_rules "$ipset_name" "$iface_name" "$fwmark" "$table" "$priority"
      ;;
      no-down-down)
	      logger -t "keenetic-pbr" "Interface $system_name is disconnected"
        logger -t "keenetic-pbr" "Removing routing rules for $system_name"
        delete_routing_rules "$iface_name" "$ipset_name" "$priority"
      ;;
    esac

    # Exit after handling the matched interface to avoid further processing
    exit 0
  fi
done
