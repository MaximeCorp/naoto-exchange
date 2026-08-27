#!/usr/bin/env python3
"""
Test client for sending Order(s) through the full system (SocketGateway's
order ingress), which now requires an authentication handshake before
accepting orders.

--- Handshake: ClientRequest (Gateways namespace) ---
Matches #pragma pack(push, 1):
    char     RequestType   (1 byte)   'A' = client details/auth request
    uint32_t ClientId       (4 bytes)
    uint8_t  Key[32]        (32 bytes)
Total: 37 bytes, tightly packed.

Each connection now represents exactly ONE client identity -- sent as the
very first message on the socket, before anything else. Because of this,
a single shared connection can no longer carry orders for two different
clients (as the old `match` mode did) -- each client now needs its own
connection, each starting with its own handshake.

--- Order wire format (unchanged from before) ---
Matches #pragma pack(push, 1) in Order.hpp. Sent as a raw native-endian
memcpy of the struct's declared field order -- parseBinOrder/serializeOrder
exist for endianness handling but are NOT currently used in the send path.

    uint32_t  Id          (4 bytes)
    int32_t   Type        (4 bytes)  0 = LIMIT, 1 = MARKET (enum class : int32_t)
    int32_t   Side        (4 bytes)  0 = BUY,   1 = SELL   (enum class : int32_t)
    int64_t   Price       (8 bytes)  ignored by the engine for MARKET orders
    int32_t   ClientId    (4 bytes)
    uint32_t  Amount      (4 bytes)
    int32_t   Asset       (4 bytes)
    int64_t   Timestamp   (8 bytes)  not worried about for now -- sent as 0

Total: 40 bytes, tightly packed.

NOTE: assumes native little-endian (x86) layout. If the system runs on a
different architecture, or parseBinOrder/serializeOrder get wired into the
send path, both format strings below need updating to match.
"""

import socket
import struct
import sys
import time

ORDER_TYPE_LIMIT = 0
ORDER_TYPE_MARKET = 1

ORDER_SIDE_BUY = 0
ORDER_SIDE_SELL = 1

# Same shared test key used throughout this project (cdp_client.py, seeded
# ClientState test data) -- the RAW secret whose SHA-256 matches the stored
# key hash on the seeded test clients.
DEFAULT_KEY_HEX = "aa92e9c3316ddc46dc882b45fe9f07b58230e83f3e3290ffbb5e2d6fa80ebd4f"

# How long to wait after sending the handshake before sending the order,
# to give the gateway time to process/accept the ClientRequest first.
HANDSHAKE_SETTLE_SECONDS = 0.5

# ---- ClientRequest (handshake) wire format ----
CLIENT_REQUEST_FORMAT = "<cI32s"
CLIENT_REQUEST_SIZE = struct.calcsize(CLIENT_REQUEST_FORMAT)
assert CLIENT_REQUEST_SIZE == 37, (
    f"Unexpected size {CLIENT_REQUEST_SIZE}, expected 37 -- check field layout"
)

# ---- OrderConfirmation (response to a submitted Order) wire format ----
# ASSUMPTION: sent in response to each Order submission (not the handshake),
# based on the OrderId/ClientOrderId field names -- correct this if the
# server actually sends it at a different point in the sequence.
ORDER_CONFIRMATION_FORMAT = "<IIB"
ORDER_CONFIRMATION_SIZE = struct.calcsize(ORDER_CONFIRMATION_FORMAT)
assert ORDER_CONFIRMATION_SIZE == 9, (
    f"Unexpected size {ORDER_CONFIRMATION_SIZE}, expected 9 -- check field layout"
)

ORDER_CONFIRMATION_STATUS = {
    0: "Accepted",
    1: "InsufficientFunds",
    2: "MaxPositions",
    3: "InvalidPrice",
    4: "InvalidQuantity",
    5: "UnknownSymbol",
    6: "TechnicalFailure",
    7: "UserNotConnected",
}


def recv_exact(sock: socket.socket, size: int) -> bytes:
    """Reads exactly `size` bytes, handling partial TCP reads."""
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError(
                f"Connection closed after {len(data)}/{size} bytes -- incomplete read."
            )
        data += chunk
    return data


def recv_order_confirmation(sock: socket.socket) -> dict:
    data = recv_exact(sock, ORDER_CONFIRMATION_SIZE)
    order_id, client_order_id, status_raw = struct.unpack(ORDER_CONFIRMATION_FORMAT, data)
    status_name = ORDER_CONFIRMATION_STATUS.get(status_raw, f"UNKNOWN({status_raw})")

    print(
        f"Received OrderConfirmation: OrderId={order_id}, ClientOrderId={client_order_id}, "
        f"Status={status_name}"
    )

    return {
        "order_id": order_id,
        "client_order_id": client_order_id,
        "status": status_name,
        "status_raw": status_raw,
    }
# '<' = little-endian, no padding
#   I -> Id        (uint32_t)
#   i -> Type       (int32_t)
#   i -> Side       (int32_t)
#   q -> Price      (int64_t)
#   i -> ClientId   (int32_t)
#   I -> Amount     (uint32_t)
#   i -> Asset      (int32_t)
#   q -> Timestamp  (int64_t)
ORDER_FORMAT = "<IiiqiIiq"
ORDER_SIZE = struct.calcsize(ORDER_FORMAT)
assert ORDER_SIZE == 40, f"Unexpected size {ORDER_SIZE}, expected 40 -- check field layout"


def build_client_request(client_id: int, key: bytes) -> bytes:
    if len(key) != 32:
        raise ValueError(f"Key must be exactly 32 bytes, got {len(key)}")

    return struct.pack(CLIENT_REQUEST_FORMAT, b"A", client_id, key)


def build_order(
    order_id: int,
    order_type: int,
    side: int,
    price: int,
    client_id: int,
    amount: int,
    asset: int,
    timestamp: int = 0,
) -> bytes:
    return struct.pack(
        ORDER_FORMAT,
        order_id,
        order_type,
        side,
        price,
        client_id,
        amount,
        asset,
        timestamp,
    )


def send_authenticated_order(
    host: str,
    port: int,
    client_id: int,
    order_bytes: bytes,
    order_description: str,
    key: bytes,
) -> dict:
    """
    Opens its own connection for `client_id`: sends the ClientRequest
    handshake first, waits for it to settle, then sends `order_bytes` on
    the same (now-authenticated) connection, and waits for the resulting
    OrderConfirmation.

    Returns the parsed confirmation dict (see recv_order_confirmation).
    """
    print(f"Connecting to {host}:{port} for client_id={client_id} ...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((host, port))

        handshake = build_client_request(client_id, key)
        print(
            f"Sending {len(handshake)}-byte ClientRequest handshake "
            f"(type='A', client_id={client_id})"
        )
        sock.sendall(handshake)

        time.sleep(HANDSHAKE_SETTLE_SECONDS)

        print(f"Sending {len(order_bytes)}-byte Order ({order_description})")
        sock.sendall(order_bytes)

        confirmation = recv_order_confirmation(sock)

        if confirmation["status"] != "Accepted":
            print(
                f"WARNING: order was NOT accepted (status={confirmation['status']}) "
                f"-- it will not be resting in the book / available to match."
            )

        return confirmation


def send_matching_pair(
    host: str,
    port: int,
    buyer_client_id: int,
    seller_client_id: int,
    asset: int,
    amount: int = 10,
    key: bytes = None,
) -> None:
    """
    Sends two orders designed to match against each other, from two DIFFERENT
    clients, each over its OWN authenticated connection (since a connection
    now belongs to exactly one client identity):
      1. A LIMIT BUY (buyer_client_id) that rests in the book -- sent and
         fully settled first, so it's there to match against.
      2. A MARKET SELL (seller_client_id) for the same asset/amount, which
         should match the resting limit buy.

    Using two different client IDs means the resulting balance changes show
    up distinctly on each side (buyer gains the asset, seller loses it) --
    unlike using the same client_id for both, which nets to zero net change
    for that one client since the buy-side and sell-side deltas cancel out.

    NOTE on `amount`: sizing this within the seller's actual held balance does
    NOT avoid the unsigned-wraparound bug identified in this project's
    matching engine (tradedAmount being negated as an unsigned type before
    reaching SoldDelta) -- that bug is a pure type issue, independent of
    whether the sell is fully covered.
    """
    if key is None:
        key = bytes.fromhex(DEFAULT_KEY_HEX)

    limit_price = 100_00  # adjust to match your engine's price scale

    limit_buy = build_order(
        order_id=1,
        order_type=ORDER_TYPE_LIMIT,
        side=ORDER_SIDE_BUY,
        price=limit_price,
        client_id=buyer_client_id,
        amount=amount,
        asset=asset,
    )

    market_sell = build_order(
        order_id=2,
        order_type=ORDER_TYPE_MARKET,
        side=ORDER_SIDE_SELL,
        price=0,  # ignored by the engine for MARKET orders
        client_id=seller_client_id,
        amount=amount,
        asset=asset,
    )

    buyer_confirmation = send_authenticated_order(
        host,
        port,
        buyer_client_id,
        limit_buy,
        f"id=1, LIMIT BUY, price={limit_price}, amount={amount}, asset={asset}",
        key,
    )

    if buyer_confirmation["status"] != "Accepted":
        print("Buyer's order was not accepted -- aborting before sending the seller's order.")
        return

    seller_confirmation = send_authenticated_order(
        host,
        port,
        seller_client_id,
        market_sell,
        f"id=2, MARKET SELL, amount={amount}, asset={asset} -- should match the resting limit buy above",
        key,
    )

    return buyer_confirmation, seller_confirmation


def main():
    if len(sys.argv) < 3:
        print(
            f"Usage: {sys.argv[0]} <host> <port> [market|limit|match] [buy|sell|<seller_client_id>] "
            f"[client_id|<asset>] [asset|<amount>] [amount] [key_hex]"
        )
        print("  market/limit: sends a single order of that type")
        print(
            "    args: [buy|sell] [client_id] [asset] [amount]  "
            "(defaults: buy, client_id=0, asset=1, amount=10)"
        )
        print("  match: sends a LIMIT BUY then a MARKET SELL that should match it,")
        print("    from two different clients, each over its own authenticated connection")
        print(
            "    args: [buyer_client_id] [seller_client_id] [asset] [amount]  "
            "(defaults: 0, 1, 1, 10)"
        )
        print("    NOTE: asset must be one both clients actually hold, or the delta is")
        print("    silently dropped -- check each client's AssetId list first.")
        print("    NOTE: sizing amount within the seller's balance does NOT avoid the")
        print("    unsigned-wraparound bug in the matching engine's delta computation.")
        sys.exit(1)

    host = sys.argv[1]
    port = int(sys.argv[2])
    order_kind = sys.argv[3] if len(sys.argv) > 3 else "limit"

    if order_kind == "match":
        buyer_client_id = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        seller_client_id = int(sys.argv[5]) if len(sys.argv) > 5 else 1
        asset = int(sys.argv[6]) if len(sys.argv) > 6 else 1
        amount = int(sys.argv[7]) if len(sys.argv) > 7 else 10
        key_hex = sys.argv[8] if len(sys.argv) > 8 else DEFAULT_KEY_HEX
        send_matching_pair(
            host,
            port,
            buyer_client_id=buyer_client_id,
            seller_client_id=seller_client_id,
            asset=asset,
            amount=amount,
            key=bytes.fromhex(key_hex),
        )
        return

    side_arg = sys.argv[4] if len(sys.argv) > 4 else "buy"
    client_id = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    asset = int(sys.argv[6]) if len(sys.argv) > 6 else 1
    amount = int(sys.argv[7]) if len(sys.argv) > 7 else 10
    key_hex = sys.argv[8] if len(sys.argv) > 8 else DEFAULT_KEY_HEX

    order_id = 1

    if order_kind == "market":
        order_type = ORDER_TYPE_MARKET
        price = 0  # ignored by the engine for MARKET orders
    else:
        order_type = ORDER_TYPE_LIMIT
        price = 100_00  # example: 100.00 if Price is fixed-point cents-scaled;
        # adjust to match whatever scale/units your engine expects for Price

    side = ORDER_SIDE_SELL if side_arg == "sell" else ORDER_SIDE_BUY

    order = build_order(
        order_id=order_id,
        order_type=order_type,
        side=side,
        price=price,
        client_id=client_id,
        amount=amount,
        asset=asset,
    )

    send_authenticated_order(
        host,
        port,
        client_id,
        order,
        f"id={order_id}, type={order_kind}, side={'BUY' if side == ORDER_SIDE_BUY else 'SELL'}, "
        f"price={price}, amount={amount}, asset={asset}",
        bytes.fromhex(key_hex),
    )


if __name__ == "__main__":
    main()