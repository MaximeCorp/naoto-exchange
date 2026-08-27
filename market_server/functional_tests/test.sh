echo "=== interface flags (looking for MULTICAST) ==="
ip link show veth0

echo ""
echo "=== RX stats (looking for drops/errors) ==="
ip -s link show veth0

echo ""
echo "=== iptables rules (any drop targeting UDP/multicast?) ==="
sudo iptables -L INPUT -n -v
sudo iptables -t raw -L -n -v 2>/dev/null

echo ""
echo "=== kernel log for drops around now ==="
sudo dmesg | tail -50

echo ""
echo "=== confirm veth0 has a route for the multicast range ==="
ip route show | grep -E "239\.|224\."

echo ""
echo "=== exact socat command + verbose output, run this WHILE sending orders ==="
socat -v UDP4-RECVFROM:30002,ip-add-membership=239.1.1.2:10.0.0.10,fork -
