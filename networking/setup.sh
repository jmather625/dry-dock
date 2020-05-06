ip netns add netns0
ip netns exec netns0 ip link set lo up
ip link add veth-default type veth peer name veth-netns0
ip link set veth-netns0 netns netns0
ip addr add 10.0.3.1/24 dev veth-default
ip netns exec netns0 ip addr add 10.0.3.2/24 dev veth-netns0
ip link set veth-default up
ip netns exec netns0 ip link set veth-netns0 up
sudo bash -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'
iptables -A FORWARD -o eth0 -i veth-default -j ACCEPT
iptables -A FORWARD -i eth0 -o veth-default -j ACCEPT
iptables -t nat -A POSTROUTING -s 10.0.3.2/24 -o eth0 -j MASQUERADE
ip netns exec netns0 ip route add default via 10.0.3.1

