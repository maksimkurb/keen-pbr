ip netns exec "$KPBR_CLIENT_NS" ip link add awg0 type dummy
ip netns exec "$KPBR_CLIENT_NS" ip link set awg0 up
ip netns exec "$KPBR_CLIENT_NS" ip link add awg1 type dummy
ip netns exec "$KPBR_CLIENT_NS" ip link set awg1 up
