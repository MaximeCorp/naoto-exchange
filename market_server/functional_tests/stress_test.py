#!/usr/bin/env python3
"""
send_buy_then_sells.py

Sends one LIMIT BUY order of size N as client 0, then N individual
MARKET SELL orders (of --sell-amount each, default 1) as client 1 --
e.g. one resting buy for N units, absorbed by N market sells hitting it
one at a time.

Protocol, straight from trading_session.hpp / wire_formats.hpp in the
dashboard's source (each client gets its own TCP connection, mirroring
TradingSession -- one per "trading panel" in the UI):

  1. TCP connect to the trading gateway.
  2. Send ClientRequest{'A', ClientId, Key}            -- 37 bytes.
  3. Wait 100ms before sending any Order -- matches TradingSession's own
     comment: the gateway's request-send thread polls two queues with a
     75ms timeout each, so an order sent immediately after auth isn't
     guaranteed to be processed in order otherwise.
  4. Send Order{...}                                    -- 40 or 64
     bytes, depending on whether the gateway build has NAOTO_PERF on
     (see --naoto-perf/--no-naoto-perf below).
  5. Optionally read back OrderConfirmation{...}         -- 9 bytes.

IMPORTANT: the NAOTO_PERF setting here MUST match whatever the real
gateway binary was actually built with, or every field from OrderId
onward gets read at the wrong byte offset on the gateway's side (this
is the exact class of bug wire_formats.hpp's own Order struct comment
warns about). Defaults to ON here, since the build command you shared
passed `-DNAOTO_PERF` -- pass --no-naoto-perf if that's ever not the
case.
"""

import argparse
import socket
import struct
import sys
import time

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8000
DEFAULT_ASSET_ID = 1
DEFAULT_KEY_HEX = "aa92e9c3316ddc46dc882b45fe9f07b58230e83f3e3290ffbb5e2d6fa80ebd4f"

# ---- Wire structs -----------------------------------------------------
# All #pragma pack(1), native little-endian, matching wire_formats.hpp
# exactly. Struct field order and widths below are copied straight from
# that header -- don't reorder/resize without re-checking it there.

CLIENT_REQUEST_FMT = "<cI32s"      # RequestType, ClientId, Key -- 37 bytes
ORDER_CONFIRMATION_FMT = "<IIB"    # OrderId, ClientOrderId, Status -- 9 bytes

ORDER_TYPE_LIMIT = 0
ORDER_TYPE_MARKET = 1
ORDER_SIDE_BUY = 0
ORDER_SIDE_SELL = 1
ORDER_ACTION_EXECUTE = 0

CONFIRMATION_STATUS = {
    0: "Accepted",
    1: "InsufficientFunds",
    2: "MaxPositions",
    3: "InvalidPrice",
    4: "InvalidQuantity",
    5: "UnknownSymbol",
    6: "TechnicalFailure",
    7: "UserNotConnected",
}


def order_format(naoto_perf: bool) -> str:
    """Struct format for Gateways::Order (see wire_formats.hpp)."""
    if naoto_perf:
        # Price, IngestedTimestamp, RoutedTimestamp, ReceivedTimestamp,
        # OrderId, ClientOrderId, ClientId, Amount, AssetId, Type, Side,
        # Action, Padding[7] -- 64 bytes total.
        return "<qQQQQIIIHBBB7s"
    # Price, OrderId, ClientOrderId, ClientId, Amount, AssetId, Type,
    # Side, Action, Padding[7] -- 40 bytes total.
    return "<qQIIIHBBB7s"


def pack_client_request(client_id: int, key: bytes) -> bytes:
    assert len(key) == 32, "key must be exactly 32 raw bytes"
    return struct.pack(CLIENT_REQUEST_FMT, b"A", client_id, key)


def pack_order(naoto_perf: bool, price: int, order_id: int, client_id: int,
               amount: int, asset_id: int, order_type: int, side: int,
               action: int = ORDER_ACTION_EXECUTE) -> bytes:
    # OrderId and ClientOrderId are set to the SAME value -- matches the
    # dashboard's own ASSUMPTION (see wire_formats.hpp's Order comment).
    # Timestamp fields (when present) are left at 0 -- the dashboard
    # never sets them either (TradingSession::send_order value-inits
    # Order{} and only fills in the fields below).
    padding = b"\x00" * 7
    if naoto_perf:
        return struct.pack(order_format(True), price, 0, 0, 0, order_id,
                            order_id, client_id, amount, asset_id,
                            order_type, side, action, padding)
    return struct.pack(order_format(False), price, order_id, order_id,
                        client_id, amount, asset_id, order_type, side,
                        action, padding)


def connect_and_auth(host: str, port: int, client_id: int,
                      key: bytes) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    req = pack_client_request(client_id, key)
    sock.sendall(req)
    print(f"[client {client_id}] connected, ClientRequest sent "
          f"({len(req)} bytes)")
    # Same 100ms wait TradingSession::connect() does before treating the
    # session as ready for orders.
    time.sleep(0.1)
    return sock


def send_order(sock: socket.socket, naoto_perf: bool, order_id: int,
               client_id: int, price: int, amount: int, asset_id: int,
               order_type: int, side: int) -> None:
    payload = pack_order(naoto_perf, price, order_id, client_id, amount,
                          asset_id, order_type, side)
    sock.sendall(payload)

def recv_exact(sock: socket.socket, num_bytes: int, timeout: float):
    """Reads exactly num_bytes, handling TCP fragmentation, or returns
    None on timeout/closed connection -- a bare sock.recv(9) can't be
    trusted to return all 9 bytes in one call on a real stream socket."""
    deadline = time.time() + timeout
    buf = b""
    try:
        while len(buf) < num_bytes:
            remaining = deadline - time.time()
            if remaining <= 0:
                return None
            sock.settimeout(remaining)
            chunk = sock.recv(num_bytes - len(buf))
            if not chunk:
                return None  # connection closed
            buf += chunk
    except socket.timeout:
        return None
    finally:
        sock.settimeout(None)
    return buf


def try_read_confirmation(sock: socket.socket, timeout: float = 0.5):
    data = recv_exact(sock, struct.calcsize(ORDER_CONFIRMATION_FMT), timeout)
    if data is None:
        return None
    order_id, client_order_id, status = struct.unpack(
        ORDER_CONFIRMATION_FMT, data)
    return {
        "order_id": order_id,
        "client_order_id": client_order_id,
        "status": CONFIRMATION_STATUS.get(status, f"Unknown({status})"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Send one LIMIT BUY order of size N as client 0, "
                    "then N MARKET SELL orders (of --sell-amount each, "
                    "default 1) as client 1.")
    parser.add_argument("n", type=int,
                        help="amount for the buy order, and number of "
                             "sell orders to send")
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help="default: %(default)s")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="default: %(default)s")
    parser.add_argument("--asset", type=int, default=DEFAULT_ASSET_ID,
                        help="AssetId to trade (default: %(default)s)")
    parser.add_argument("--price", type=int, default=1,
                        help="Price for the limit buy order -- arbitrary, "
                             "adjust to whatever makes sense for your "
                             "asset (default: %(default)s)")
    parser.add_argument("--sell-amount", type=int, default=1,
                        help="Amount per individual market sell order "
                             "(default: %(default)s, so N sells of 1 "
                             "exactly consume the N-unit buy)")
    parser.add_argument("--key", default=DEFAULT_KEY_HEX,
                        help="Hex-encoded 32-byte client key, same for "
                             "both clients (default: the key you gave)")
    parser.add_argument("--naoto-perf", dest="naoto_perf",
                        action="store_true", default=True,
                        help="Order struct includes the 3 NAOTO_PERF "
                             "timestamp fields, 64 bytes total. Default "
                             "-- matches the -DNAOTO_PERF build flag.")
    parser.add_argument("--no-naoto-perf", dest="naoto_perf",
                        action="store_false",
                        help="Use the 40-byte Order layout instead (no "
                             "timestamp fields) -- only if the gateway "
                             "was built WITHOUT NAOTO_PERF.")
    parser.add_argument("--no-confirmations", action="store_true",
                        help="Don't wait for/print OrderConfirmation "
                             "replies")
    args = parser.parse_args()

    if args.n <= 0:
        parser.error("N must be a positive integer")

    try:
        key = bytes.fromhex(args.key)
    except ValueError:
        parser.error("--key must be a valid hex string")
    if len(key) != 32:
        parser.error(f"--key must decode to exactly 32 bytes, got {len(key)}")

    order_size = struct.calcsize(order_format(args.naoto_perf))
    print(f"Order layout: {'NAOTO_PERF (64 bytes)' if args.naoto_perf else 'no NAOTO_PERF (40 bytes)'} "
          f"-- sizeof(Order)={order_size}")
    print(f"Target: {args.host}:{args.port}, asset {args.asset}\n")

    # ---- Client 0: one resting LIMIT BUY of size N -----------------------
    sock_a = connect_and_auth(args.host, args.port, client_id=0, key=key)
    try:
        send_order(sock_a, args.naoto_perf, order_id=1, client_id=0,
                   price=args.price, amount=args.n, asset_id=args.asset,
                   order_type=ORDER_TYPE_LIMIT, side=ORDER_SIDE_BUY)
        if not args.no_confirmations:
            conf = try_read_confirmation(sock_a)
            print(f"[client 0] confirmation: {conf}" if conf else
                  "[client 0] no confirmation received within timeout")
    finally:
        sock_a.close()

    print()

    time.sleep(1)

    sent_count = 0

    # ---- Client 1: N individual MARKET SELL orders ------------------------
    sock_b = connect_and_auth(args.host, args.port, client_id=1, key=key)
    try:
        for i in range(args.n):
            order_id = i + 1
            send_order(sock_b, args.naoto_perf, order_id=order_id,
                       client_id=1, price=0,  # ignored for MARKET orders
                       amount=args.sell_amount, asset_id=args.asset,
                       order_type=ORDER_TYPE_MARKET, side=ORDER_SIDE_SELL)
            sent_count += 1
    finally:
        sock_b.close()

    print("\nDone.")

    print(f"Sent {sent_count} orders.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())