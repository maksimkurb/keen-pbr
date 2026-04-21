ip netns add "$KPBR_SERVER_NS"
ip netns exec "$KPBR_SERVER_NS" ip link set lo up

ip link add wan_fast netns "$KPBR_CLIENT_NS" type veth peer name srv_fast netns "$KPBR_SERVER_NS"
ip netns exec "$KPBR_CLIENT_NS" ip addr add 10.203.0.2/24 dev wan_fast
ip netns exec "$KPBR_CLIENT_NS" ip link set wan_fast up
ip netns exec "$KPBR_SERVER_NS" ip addr add 10.203.0.1/24 dev srv_fast
ip netns exec "$KPBR_SERVER_NS" ip link set srv_fast up

ip netns exec "$KPBR_CLIENT_NS" ip link add wan_dead type dummy
ip netns exec "$KPBR_CLIENT_NS" ip link set wan_dead up

ip netns exec "$KPBR_SERVER_NS" python3 /opt/keen-pbr/firewall-it/scripts/urltest_server.py --host 10.203.0.1 --port 18080 &
server_pid="$!"
