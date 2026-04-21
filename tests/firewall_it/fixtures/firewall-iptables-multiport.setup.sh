ip netns exec "$KPBR_CLIENT_NS" ip link add wan0 type dummy
ip netns exec "$KPBR_CLIENT_NS" ip link set wan0 up
