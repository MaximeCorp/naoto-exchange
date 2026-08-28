#!/bin/bash
#
# setup_dpdk_test_env.sh
#
# Recreates the local dev/test environment needed to run the DPDK-based
# binaries (MatchingEngine, CDP, SocketGateway) over AF_PACKET on a veth
# pair. Neither hugepages nor the veth pair persist across a reboot, so
# this needs to be re-run after every restart, before launching any of the
# DPDK binaries.
#
# Usage:
#   sudo ./setup_dpdk_test_env.sh
#
set -euo pipefail

HUGEPAGE_COUNT=1024
HUGEPAGE_DIR="/dev/hugepages"
VETH_A="veth0"
VETH_B="veth1"

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root (it configures hugepages and network interfaces)." >&2
    echo "Try: sudo $0" >&2
    exit 1
fi

echo "== Reserving hugepages =="
HUGEPAGE_NR_PATH="/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
if [[ ! -f "$HUGEPAGE_NR_PATH" ]]; then
    echo "ERROR: $HUGEPAGE_NR_PATH not found -- is this kernel built with hugepage support?" >&2
    exit 1
fi

echo "$HUGEPAGE_COUNT" > "$HUGEPAGE_NR_PATH"
ACTUAL_COUNT=$(cat "$HUGEPAGE_NR_PATH")

if [[ "$ACTUAL_COUNT" -lt "$HUGEPAGE_COUNT" ]]; then
    echo "WARNING: requested $HUGEPAGE_COUNT hugepages but only got $ACTUAL_COUNT" >&2
    echo "         (system may be low on contiguous free memory -- consider freeing memory" >&2
    echo "         or lowering HUGEPAGE_COUNT in this script)." >&2
else
    echo "Reserved $ACTUAL_COUNT x 2048kB hugepages."
fi

echo "== Ensuring hugetlbfs is mounted at $HUGEPAGE_DIR =="
if mount | grep -q "on $HUGEPAGE_DIR type hugetlbfs"; then
    echo "$HUGEPAGE_DIR is already mounted as hugetlbfs."
else
    mkdir -p "$HUGEPAGE_DIR"
    mount -t hugetlbfs nodev "$HUGEPAGE_DIR"
    echo "Mounted hugetlbfs at $HUGEPAGE_DIR."
fi

echo "== Setting up veth pair ($VETH_A <-> $VETH_B) =="
if ip link show "$VETH_A" &>/dev/null; then
    echo "$VETH_A already exists, skipping creation."
else
    ip link add "$VETH_A" type veth peer name "$VETH_B"
    echo "Created veth pair: $VETH_A <-> $VETH_B"
fi

ip link set "$VETH_A" up
ip link set "$VETH_B" up
echo "Brought $VETH_A and $VETH_B up."

echo "== Disabling reverse-path filtering (rp_filter) on both veth interfaces =="
# Without this, the kernel's strict-mode rp_filter can silently drop
# locally-sourced multicast traffic on a veth pair that has no "normal"
# routing table entry backing it -- a common gotcha for exactly this kind
# of local test setup. See: net.ipv4.conf.<iface>.rp_filter in
# https://www.kernel.org/doc/Documentation/networking/ip-sysctl.txt
sudo sysctl -w net.ipv4.conf."$VETH_A".rp_filter=0 > /dev/null
sudo sysctl -w net.ipv4.conf."$VETH_B".rp_filter=0 > /dev/null
sudo sysctl -w net.ipv4.conf.all.rp_filter=0 > /dev/null
echo "rp_filter disabled on $VETH_A and $VETH_B."

echo ""
echo "== Environment ready =="
echo "Hugepages: $ACTUAL_COUNT x 2048kB at $HUGEPAGE_DIR"
ip link show "$VETH_A"
ip link show "$VETH_B"
echo "rp_filter($VETH_A) = $(cat /proc/sys/net/ipv4/conf/"$VETH_A"/rp_filter)"
echo "rp_filter($VETH_B) = $(cat /proc/sys/net/ipv4/conf/"$VETH_B"/rp_filter)"