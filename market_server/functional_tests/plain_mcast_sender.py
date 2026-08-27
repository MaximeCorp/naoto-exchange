#!/usr/bin/env python3
"""plain_mcast_sender.py — sanity-check multicast delivery without DPDK."""
import socket
import time

GROUP = "239.1.1.1"
PORT = 30001

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton("10.0.0.20"))

for i in range(5):
    msg = f"hello {i}".encode()
    sock.sendto(msg, (GROUP, PORT))
    print(f"sent: {msg}")
    time.sleep(1)
