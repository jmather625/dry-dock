sudo ip netns delete netns0
sudo bash -c 'echo 0 > /proc/sys/net/ipv4/ip_forward'
