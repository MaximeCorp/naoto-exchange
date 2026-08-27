#!/usr/bin/env python3
"""
test_loopback_multicast.py

Isolates whether multicast socket delivery works at all on this machine,
independent of veth/DPDK. Runs a receiver and sender against 127.0.0.1
instead of the veth pair.

Usage:
    python3 test_loopback_multicast.py receive
    python3 test_loopback_multicast.py send
"""
import socket
import struct
import sys
import time

GROUP = "239.1.1.1"
PORT = 30001
LOOPBACK_IF = "127.0.0.1"


def run_receiver():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    try:
        sock.bind(("", PORT))
    except OSError as e:
        print(f"bind failed: {e}")
        return

    mreq = struct.pack("4s4s", socket.inet_aton(GROUP),
                       socket.inet_aton(LOOPBACK_IF))
    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    except OSError as e:
        print(f"IP_ADD_MEMBERSHIP failed: {e}")
        return

    print(f"Listening on {GROUP}:{PORT} via {LOOPBACK_IF} ...")
    print("(Ctrl+C to stop)")

    try:
        while True:
            data, addr = sock.recvfrom(1024)
            print(f"RECEIVED {len(data)} bytes from {addr}: {data!r}")
    except KeyboardInterrupt:
        print("\nStopping receiver.")


def run_sender():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                    socket.inet_aton(LOOPBACK_IF))

    print(f"Sending to {GROUP}:{PORT} via {LOOPBACK_IF} ...")
    for i in range(10):
        msg = f"hello {i}".encode()
        sock.sendto(msg, (GROUP, PORT))
        print(f"sent: {msg}")
        time.sleep(1)


if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in ("send", "receive"):
        print("usage: test_loopback_multicast.py [send|receive]")
        sys.exit(1)

    if sys.argv[1] == "receive":
        run_receiver()
    else:
        run_sender()