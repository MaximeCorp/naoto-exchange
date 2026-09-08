#!/bin/bash
#
# setup_dpdk_test_env.sh  (v2 -- idempotent + self-verifying)
#
# Recreates the local dev/test environment for the DPDK-based binaries
# (MatchingEngine, CDP, SocketGateway) over AF_PACKET on a veth pair,
# AND verifies that kernel multicast actually works over it before
# handing back control.
#
# Differences from v1, and why:
#   1. TEARS DOWN UNCONDITIONALLY FIRST. v1 skipped creation if veth0
#      already existed, but still tried to assign addresses -- so a
#      partially-torn-down or half-modified pair produced a silently
#      half-configured result. Re-running it could leave the machine in
#      a state that looked configured but wasn't. This version always
#      starts from nothing, so re-running is always safe and always
#      lands in the same place.
#   2. CLEANS UP STRAY NETWORK NAMESPACES holding a veth end. An
#      interface moved into a namespace disappears from the root
#      namespace, which makes it look deleted while still existing --
#      a genuinely confusing state to debug.
#   3. VERIFIES AT THE END with a real kernel multicast send/receive
#      over the pair. Everything before this point is configuration
#      that *should* work; this proves it does. Without this, a broken
#      environment is only discovered later, while debugging something
#      else entirely.
#
# Usage:
#   sudo ./setup_dpdk_test_env.sh
#
set -euo pipefail

HUGEPAGE_COUNT=1024
HUGEPAGE_DIR="/dev/hugepages"
VETH_A="veth0"
VETH_B="veth1"
VETH_A_IP="10.10.10.1"
VETH_B_IP="10.10.10.2"
VETH_PREFIX="24"
# Must match the dashboard's config (config::kOrderBookMcastGroup etc.)
VERIFY_GROUP="239.1.1.2"
VERIFY_PORT=30002

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root (it configures hugepages and network interfaces)." >&2
    echo "Try: sudo $0" >&2
    exit 1
fi

echo "================================================================"
echo " Tearing down any existing state"
echo "================================================================"

# A veth end living inside a namespace is invisible from the root
# namespace -- it looks deleted but isn't. Clear any namespace that
# holds one of our interfaces.
if ip netns list &>/dev/null; then
    for ns in $(ip netns list 2>/dev/null | awk '{print $1}'); do
        if ip netns exec "$ns" ip link show "$VETH_B" &>/dev/null || \
           ip netns exec "$ns" ip link show "$VETH_A" &>/dev/null; then
            echo "Namespace '$ns' holds one of our veth ends -- deleting it."
            ip netns del "$ns"
        fi
    done
fi

# Deleting either end removes the whole pair. Both attempted in case
# they somehow ended up unpaired.
for iface in "$VETH_A" "$VETH_B"; do
    if ip link show "$iface" &>/dev/null; then
        echo "Deleting existing $iface."
        ip link del "$iface" 2>/dev/null || true
    fi
done

if ip link show "$VETH_A" &>/dev/null || ip link show "$VETH_B" &>/dev/null; then
    echo "ERROR: veth interfaces still present after deletion attempt." >&2
    ip link show | grep -E "veth" >&2 || true
    exit 1
fi
echo "Clean: no veth0/veth1 present."

echo ""
echo "================================================================"
echo " Reserving hugepages"
echo "================================================================"
HUGEPAGE_NR_PATH="/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
if [[ ! -f "$HUGEPAGE_NR_PATH" ]]; then
    echo "ERROR: $HUGEPAGE_NR_PATH not found -- is this kernel built with hugepage support?" >&2
    exit 1
fi
echo "$HUGEPAGE_COUNT" > "$HUGEPAGE_NR_PATH"
ACTUAL_COUNT=$(cat "$HUGEPAGE_NR_PATH")
if [[ "$ACTUAL_COUNT" -lt "$HUGEPAGE_COUNT" ]]; then
    echo "WARNING: requested $HUGEPAGE_COUNT hugepages but only got $ACTUAL_COUNT" >&2
    echo "         (system may be low on contiguous free memory)." >&2
else
    echo "Reserved $ACTUAL_COUNT x 2048kB hugepages."
fi

if mount | grep -q "on $HUGEPAGE_DIR type hugetlbfs"; then
    echo "$HUGEPAGE_DIR already mounted as hugetlbfs."
else
    mkdir -p "$HUGEPAGE_DIR"
    mount -t hugetlbfs nodev "$HUGEPAGE_DIR"
    echo "Mounted hugetlbfs at $HUGEPAGE_DIR."
fi

echo ""
echo "================================================================"
echo " Creating veth pair"
echo "================================================================"
ip link add "$VETH_A" type veth peer name "$VETH_B"
ip link set "$VETH_A" up
ip link set "$VETH_B" up
ip addr add "${VETH_A_IP}/${VETH_PREFIX}" dev "$VETH_A"
ip addr add "${VETH_B_IP}/${VETH_PREFIX}" dev "$VETH_B"
ip link set "$VETH_A" multicast on
ip link set "$VETH_B" multicast on
echo "Created $VETH_A ($VETH_A_IP) <-> $VETH_B ($VETH_B_IP), both up, multicast on."

echo ""
echo "================================================================"
echo " Disabling reverse-path filtering"
echo "================================================================"
# Both ends of this pair sit on the SAME subnet on the SAME host, so a
# packet from 10.10.10.1 arriving on veth1 has a reverse-path lookup
# that resolves to veth0 -- the "wrong" interface -- and strict rp_filter
# discards it before it ever reaches a socket. The kernel uses
# max(conf.all, conf.<iface>), so 'all' must be zeroed too, not just the
# per-interface values. Note these are per-interface settings that are
# LOST whenever the interface is recreated -- which is exactly why this
# script must set them every time rather than assuming they persist.
sysctl -w net.ipv4.conf.all.rp_filter=0 > /dev/null
sysctl -w net.ipv4.conf.default.rp_filter=0 > /dev/null
sysctl -w net.ipv4.conf."$VETH_A".rp_filter=0 > /dev/null
sysctl -w net.ipv4.conf."$VETH_B".rp_filter=0 > /dev/null
echo "rp_filter=0 on all, default, $VETH_A, $VETH_B."

echo ""
echo "================================================================"
echo " Verifying kernel multicast actually works over the pair"
echo "================================================================"
echo "Sending a test datagram to ${VERIFY_GROUP}:${VERIFY_PORT} via $VETH_A,"
echo "listening on $VETH_B. This uses ordinary kernel sockets (no DPDK),"
echo "so it isolates the environment from anything application-specific."
echo ""

VERIFY_RESULT=$(python3 - "$VETH_B" "$VETH_A_IP" "$VERIFY_GROUP" "$VERIFY_PORT" <<'PYEOF'
import socket, struct, sys, threading, time

iface, src_ip, group, port = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
received = []

def listener(ready):
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("", port))
    # ip_mreqn: multiaddr(4) + address(4) + ifindex(4)
    ifindex = socket.if_nametoindex(iface)
    mreq = struct.pack("4s4si", socket.inet_aton(group), socket.inet_aton("0.0.0.0"), ifindex)
    rx.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    rx.settimeout(4.0)
    ready.set()
    try:
        data, addr = rx.recvfrom(65536)
        received.append((len(data), addr))
    except socket.timeout:
        pass
    finally:
        rx.close()

ready = threading.Event()
t = threading.Thread(target=listener, args=(ready,), daemon=True)
t.start()
ready.wait(2.0)
time.sleep(0.3)  # let the join settle

tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(src_ip))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
for _ in range(5):
    tx.sendto(b"MCASTTEST" * 3, (group, port))
    time.sleep(0.2)
tx.close()

t.join(timeout=5.0)
print("OK" if received else "FAIL")
PYEOF
)

echo ""
if [[ "$VERIFY_RESULT" == "OK" ]]; then
    echo "*** VERIFIED: kernel multicast traverses $VETH_A -> $VETH_B correctly. ***"
    echo ""
    echo "The environment is sound. If the dashboard still receives nothing"
    echo "after this, the problem is on the DPDK sending side or in the"
    echo "application -- NOT in this network setup."
else
    echo "*** VERIFICATION FAILED: no multicast received over the pair. ***" >&2
    echo "" >&2
    echo "The environment is NOT working. Do not spend time debugging the" >&2
    echo "application until this passes -- it cannot possibly receive." >&2
    echo "" >&2
    echo "Things to check, in order:" >&2
    echo "  1. cat /proc/sys/net/ipv4/conf/${VETH_B}/rp_filter   (expect 0)" >&2
    echo "  2. cat /proc/sys/net/ipv4/conf/all/rp_filter          (expect 0)" >&2
    echo "  3. ip maddr show dev ${VETH_B} | grep ${VERIFY_GROUP}  (during a run)" >&2
    echo "  4. Any firewall (iptables/nft) rules touching UDP ${VERIFY_PORT} or 239.0.0.0/8" >&2
    exit 1
fi

echo ""
echo "================================================================"
echo " Final state"
echo "================================================================"
ip addr show "$VETH_A" | grep -E "^[0-9]+:|inet "
ip addr show "$VETH_B" | grep -E "^[0-9]+:|inet "
echo "rp_filter(all)      = $(cat /proc/sys/net/ipv4/conf/all/rp_filter)"
echo "rp_filter($VETH_A)  = $(cat /proc/sys/net/ipv4/conf/"$VETH_A"/rp_filter)"
echo "rp_filter($VETH_B)  = $(cat /proc/sys/net/ipv4/conf/"$VETH_B"/rp_filter)"
echo "hugepages           = $ACTUAL_COUNT x 2048kB at $HUGEPAGE_DIR"
echo ""
echo "Ready. Start the DPDK binaries, then run the dashboard with:"
echo "    MCAST_IFACE=$VETH_B ./trading_dashboard"