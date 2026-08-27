#!/usr/bin/env python3
"""receive_market_data.py — listen on both multicast feeds and decode.

Changes vs previous version:
- LOCAL_IF comment updated: 10.0.0.10 now lives on br0 (bridge), not veth0.
- Decoders now loop over the datagram: the engine batches multiple structs
  per UDP packet (e.g. 72-byte datagrams = 2 x 36-byte OrderStateReport),
  so parsing only the first struct silently dropped the rest.
- Trailing partial bytes (len % size != 0) are reported instead of ignored.
"""
import socket
import struct
import select
import ctypes
from order_defs import OrderBookUpdate, OrderStateReport

BOOK_GROUP, BOOK_PORT = "239.1.1.2", 30002
ORDER_GROUP, ORDER_PORT = "239.1.1.1", 30001
LOCAL_IF = "10.0.0.10"  # br0's address (IP moved from veth0 to br0)


def make_mcast_socket(group, port, iface_ip):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))
    mreq = struct.pack("4s4s", socket.inet_aton(group), socket.inet_aton(iface_ip))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    return sock


def decode_book_update(data):
    size = ctypes.sizeof(OrderBookUpdate)
    if len(data) < size:
        print(f"[BOOK] short packet ({len(data)}B, need {size}): {data.hex()}")
        return
    count = len(data) // size
    for i in range(count):
        chunk = data[i * size:(i + 1) * size]
        update = OrderBookUpdate.from_buffer_copy(chunk)
        side_str = "BUY" if update.Side == 0 else "SELL"
        print(f"[BOOK]   seq={update.SequenceId} depth={update.Depth} "
              f"price={update.Price} asset={update.AssetId} side={side_str}")
    rem = len(data) % size
    if rem:
        print(f"[BOOK] WARNING: {rem} trailing bytes ignored "
              f"(datagram {len(data)}B not a multiple of {size}B)")


def decode_order_report(data):
    size = ctypes.sizeof(OrderStateReport)
    if len(data) < size:
        print(f"[REPORT] short packet ({len(data)}B, need {size}): {data.hex()}")
        return
    count = len(data) // size
    for i in range(count):
        chunk = data[i * size:(i + 1) * size]
        report = OrderStateReport.from_buffer_copy(chunk)
        print(f"[REPORT] seq={report.SequenceId} "
              f"bought={report.BoughtDelta}(asset {report.BoughtAssetId}) "
              f"sold={report.SoldDelta}(asset {report.SoldAssetId}) "
              f"client={report.ClientId} order={report.OrderId} trade={report.TradeId}")
    rem = len(data) % size
    if rem:
        print(f"[REPORT] WARNING: {rem} trailing bytes ignored "
              f"(datagram {len(data)}B not a multiple of {size}B)")


if __name__ == "__main__":
    print(f"sizeof(OrderBookUpdate) = {ctypes.sizeof(OrderBookUpdate)}")
    print(f"sizeof(OrderStateReport) = {ctypes.sizeof(OrderStateReport)}")
    print("(compare both against C++ sizeof(...) to confirm layout match)\n")

    book_sock = make_mcast_socket(BOOK_GROUP, BOOK_PORT, LOCAL_IF)
    order_sock = make_mcast_socket(ORDER_GROUP, ORDER_PORT, LOCAL_IF)

    print(f"Listening on {BOOK_GROUP}:{BOOK_PORT} (book) and "
          f"{ORDER_GROUP}:{ORDER_PORT} (orders)...")

    sockets = [book_sock, order_sock]
    try:
        while True:
            readable, _, _ = select.select(sockets, [], [])
            for sock in readable:
                data, addr = sock.recvfrom(9000)
                if sock is book_sock:
                    decode_book_update(data)
                else:
                    decode_order_report(data)
    except KeyboardInterrupt:
        print("\nStopping.")