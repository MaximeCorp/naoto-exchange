#!/usr/bin/env bash
# setup_test_env.sh — recreate the local DPDK test topology after reboot.
# Run with: sudo bash setup_test_env.sh
#
# Topology:  [engine/DPDK af_packet on veth1 (10.0.0.20)] <-> veth0 -> br0 (10.0.0.10) -> kernel sockets
#
# Root cause of the original "multicast never reaches sockets" bug:
#   bare veth pair -> replaced with bridge; bridge IGMP snooping had an EMPTY
#   MDB (no querier on isolated segment) -> disable snooping so multicast is
#   flooded like broadcast, including to the host port.
set -euo pipefail

# Clean slate (ignore errors if absent)
ip link del veth0 2>/dev/null || true
ip link del br0   2>/dev/null || true

# veth pair + bridge
ip link add veth0 type veth peer name veth1
ip link add name br0 type bridge
ip link set veth0 master br0

ip link set br0 up
ip link set veth0 up
ip link set veth1 up

# IP goes on br0 (NOT veth0) and on veth1
ip addr add 10.0.0.10/24 dev br0
ip addr add 10.0.0.20/24 dev veth1

# THE FIX: disable IGMP snooping so multicast floods to the host port
echo 0 > /sys/class/net/br0/bridge/multicast_snooping

# Belt & suspenders: source validation relaxations (packets from 10.0.0.20,
# a local address, arrive on br0)
sysctl -w net.ipv4.conf.all.rp_filter=0
sysctl -w net.ipv4.conf.br0.rp_filter=0
sysctl -w net.ipv4.conf.veth1.rp_filter=0
sysctl -w net.ipv4.conf.all.accept_local=1
sysctl -w net.ipv4.conf.br0.accept_local=1

echo
echo "Topology ready:"
ip -brief addr show br0 veth0 veth1
echo
echo "Snooping: $(cat /sys/class/net/br0/bridge/multicast_snooping) (0 = disabled, correct)"
echo
echo "Next steps:"
echo "  1. python3 receive_market_data.py            # join groups FIRST"
echo "  2. sudo ./MarketExecution --file-prefix=send -l 0-3 -n 2 --no-huge \\"
echo "       --vdev=net_af_packet0,iface=veth1,qpairs=2 -- --portId 0"
echo "  3. python3 send_orders.py scripted"
