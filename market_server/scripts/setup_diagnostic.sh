#!/usr/bin/env bash
#
# multicast_diagnostic.sh
#
# Checks OS-level multicast configuration relevant to the dashboard's
# expected feeds (239.1.1.1:30001, 239.1.1.2:30002 by default -- edit
# GROUPS below if you've overridden them via MCAST_ORDER_STATE_ADDR /
# MCAST_ORDER_BOOK_ADDR), and captures live wire traffic to compare
# against what the dashboard's recv() actually sees.
#
# Run with sudo for the tcpdump section to work (interface/rp_filter/
# firewall checks work either way, just may show less without sudo).
#
#   sudo ./multicast_diagnostic.sh [interface_name]
#
# interface_name is optional -- only used to highlight that specific
# interface's rp_filter setting; every interface is still checked.

set -uo pipefail

IFACE="${1:-}"
GROUPS=("239.1.1.1:30001" "239.1.1.2:30002")
PORTS=(30001 30002)

hr() { printf '%s\n' "----------------------------------------------------------------"; }

echo "================================================================"
echo " Multicast diagnostic -- $(date)"
[ -n "$IFACE" ] && echo " Interface of interest: $IFACE"
echo "================================================================"

echo
hr
echo "1. Interfaces and multicast capability"
hr
if command -v ip >/dev/null 2>&1; then
    ip -o link show | while IFS= read -r line; do
        ifname=$(echo "$line" | awk -F': ' '{print $2}')
        if echo "$line" | grep -q "MULTICAST"; then
            echo "  $ifname: MULTICAST supported"
        else
            echo "  $ifname: MULTICAST NOT supported  <-- cannot join groups on this interface"
        fi
    done
else
    echo "  'ip' command not found -- falling back to ifconfig"
    ifconfig -a 2>/dev/null | grep -B1 "MULTICAST\|flags" || echo "  ifconfig also unavailable"
fi

echo
hr
echo "2. rp_filter (reverse-path filtering) -- non-zero can silently drop"
echo "   multicast packets whose source looks 'wrong' for the arriving"
echo "   interface. Fix with: sysctl -w net.ipv4.conf.<iface>.rp_filter=0"
hr
if [ -r /proc/sys/net/ipv4/conf/all/rp_filter ]; then
    echo "  all:     $(cat /proc/sys/net/ipv4/conf/all/rp_filter)"
    echo "  default: $(cat /proc/sys/net/ipv4/conf/default/rp_filter)"
    for d in /proc/sys/net/ipv4/conf/*/rp_filter; do
        name=$(basename "$(dirname "$d")")
        [ "$name" = "all" ] || [ "$name" = "default" ] && continue
        val=$(cat "$d")
        marker=""
        [ "$val" != "0" ] && marker="  <-- non-zero"
        echo "  $name: $val$marker"
    done
else
    echo "  /proc/sys/net/ipv4/conf not readable"
fi

echo
hr
echo "3. Currently joined multicast group memberships (per interface)"
hr
if [ -r /proc/net/igmp ]; then
    cat /proc/net/igmp
else
    echo "  /proc/net/igmp not readable"
fi
echo
if command -v ip >/dev/null 2>&1; then
    echo "  (ip maddr show -- human-readable, check this one)"
    ip maddr show 2>/dev/null
    echo
    echo "  Check above for 239.1.1.1 and 239.1.1.2 (or your overridden"
    echo "  MCAST_ORDER_STATE_ADDR/MCAST_ORDER_BOOK_ADDR groups) listed"
    echo "  against the interface you expect. If a group isn't listed at"
    echo "  all, the IP_ADD_MEMBERSHIP join never registered, or was"
    echo "  dropped after."
fi

echo
hr
echo "4. Firewall rules that might touch these ports / the 239.x range"
hr
if command -v iptables >/dev/null 2>&1; then
    echo "  iptables (may require sudo to see real rules):"
    iptables -L -n -v 2>/dev/null | grep -iE "udp|239\.|drop|reject" \
        || echo "    (nothing obviously relevant found)"
else
    echo "  iptables not installed"
fi
if command -v nft >/dev/null 2>&1; then
    echo
    echo "  nftables ruleset:"
    nft list ruleset 2>/dev/null | grep -iE "udp|239\.|drop|reject" \
        || echo "    (nothing obviously relevant found, or requires sudo)"
fi

echo
hr
echo "5. Live wire capture -- THE decisive check"
echo "   If tcpdump ALSO shows length 0 (or 'UDP, length 0') on the wire,"
echo "   the sender itself is emitting empty payloads -- not a receiver or"
echo "   OS config issue on this box at all. If tcpdump shows a REAL"
echo "   payload length here but the dashboard still logs 0 bytes, that's"
echo "   the opposite conclusion and worth reporting back."
hr
if command -v tcpdump >/dev/null 2>&1; then
    echo "  Capturing up to 10 packets on ports ${PORTS[*]} (10s timeout)..."
    echo "  (needs sudo/CAP_NET_RAW to capture; run this whole script with sudo if empty)"
    timeout 10 tcpdump -i any -n -v "udp port ${PORTS[0]} or udp port ${PORTS[1]}" -c 10 2>&1 \
        | tee /tmp/mcast_diag_tcpdump.txt
    echo
    echo "  Full output also saved to /tmp/mcast_diag_tcpdump.txt"
else
    echo "  tcpdump not installed. Install it for the single most useful"
    echo "  check here:  sudo apt install tcpdump"
fi

echo
echo "================================================================"
echo " Done."
echo "================================================================"