#!/usr/bin/env python3
"""
Test client for ClientDetailsProvider (CDP).

Wire format for ClientRequest (matches #pragma pack(push, 1) in ClientRequest.hpp):
    char     RequestType   (1 byte)   'A' = client details request, 'D' = disconnect
    uint32_t ClientId       (4 bytes, little-endian -- adjust if your platform/wire differs)
    uint8_t  Key[32]        (32 bytes)
    uint32_t ClientFd       (4 bytes)
    uint16_t GatewayId      (2 bytes)

Total size: 1 + 4 + 32 + 4 + 2 = 43 bytes, tightly packed (no padding, due to #pragma pack(1)).

NOTE: struct.pack format string below assumes little-endian (x86) byte order to
match your host's native layout, since the struct is a raw memcpy'd C struct,
not something that goes through your DPDK code's explicit rte_cpu_to_be_*
conversions (those were only for the multicast IP/port fields in the UDP
receiver, not for this TCP/epoll client-facing struct). If EpollServer or
ClientRequest ever apply byte-swapping before/after the socket read, this
needs to match that instead.
"""

import socket
import struct
import sys

# ---- GatewayConnection wire format (first message on every new connection) ----
# Matches #pragma pack(push, 1) in GatewayConnection.hpp:
#   uint16_t GatewayId   (2 bytes)
GATEWAY_CONNECTION_FORMAT = "<H"
GATEWAY_CONNECTION_SIZE = struct.calcsize(GATEWAY_CONNECTION_FORMAT)
assert GATEWAY_CONNECTION_SIZE == 2, (
    f"Unexpected size {GATEWAY_CONNECTION_SIZE}, expected 2"
)


def build_gateway_connection(gateway_id: int) -> bytes:
    return struct.pack(GATEWAY_CONNECTION_FORMAT, gateway_id)


# ---- ClientRequest wire format ----
# '<' = little-endian, no padding (we lay out fields manually to match #pragma pack(1) exactly)
#   c    -> RequestType (char, 1 byte)
#   I    -> ClientId (uint32_t, 4 bytes)
#   32s  -> Key (32 raw bytes)
#   I    -> ClientFd (uint32_t, 4 bytes)
#   H    -> GatewayId (uint16_t, 2 bytes)
REQUEST_FORMAT = "<cI32sIH"
REQUEST_SIZE = struct.calcsize(REQUEST_FORMAT)
assert REQUEST_SIZE == 43, f"Unexpected size {REQUEST_SIZE}, expected 43 -- check padding/format"


def build_request(
    request_type: bytes,
    client_id: int,
    key: bytes,
    client_fd: int,
    gateway_id: int,
) -> bytes:
    if len(key) != 32:
        raise ValueError(f"Key must be exactly 32 bytes, got {len(key)}")
    if len(request_type) != 1:
        raise ValueError("RequestType must be a single byte, e.g. b'A' or b'D'")

    return struct.pack(
        REQUEST_FORMAT,
        request_type,
        client_id,
        key,
        client_fd,
        gateway_id,
    )


# ---- ClientRequestResponse<MaxPositions> wire format ----
# Matches #pragma pack(push, 1), field order exactly as declared:
#   char     Status                       (1 byte)
#   uint64_t SequenceId                   (8 bytes)
#   uint32_t ClientId                     (4 bytes)
#   uint32_t ClientFd                     (4 bytes)
#   uint16_t AssetId[MaxPositions]        (2 * MaxPositions bytes)
#   int64_t  Confirmed[MaxPositions]      (8 * MaxPositions bytes)
#   int64_t  Attempt[MaxPositions]        (8 * MaxPositions bytes)
MAX_POSITIONS = 16  # matches MaxPositions template param used elsewhere in this conversation

RESPONSE_FORMAT = "<cQII{n}H{n}q{n}q".format(n=MAX_POSITIONS)
RESPONSE_SIZE = struct.calcsize(RESPONSE_FORMAT)
_expected_size = 1 + 8 + 4 + 4 + (2 * MAX_POSITIONS) + (8 * MAX_POSITIONS) + (8 * MAX_POSITIONS)
assert RESPONSE_SIZE == _expected_size, (
    f"RESPONSE_SIZE={RESPONSE_SIZE} does not match expected {_expected_size} "
    f"-- check MAX_POSITIONS matches the server's compiled MaxPositions"
)


def parse_response(data: bytes):
    if len(data) != RESPONSE_SIZE:
        print(
            f"WARNING: received {len(data)} bytes, expected exactly {RESPONSE_SIZE} "
            f"(MAX_POSITIONS={MAX_POSITIONS}) -- struct layout mismatch or partial read?"
        )
        return None

    unpacked = struct.unpack(RESPONSE_FORMAT, data)
    status = unpacked[0]
    sequence_id = unpacked[1]
    client_id = unpacked[2]
    client_fd = unpacked[3]
    offset = 4
    asset_ids = unpacked[offset : offset + MAX_POSITIONS]
    offset += MAX_POSITIONS
    confirmed = unpacked[offset : offset + MAX_POSITIONS]
    offset += MAX_POSITIONS
    attempt = unpacked[offset : offset + MAX_POSITIONS]

    status_str = status.decode("ascii", errors="replace")
    print(f"Status:      {status_str!r}")
    print(f"SequenceId:  {sequence_id}")
    print(f"ClientId:    {client_id}")
    print(f"ClientFd:    {client_fd}")
    print(f"{'AssetId':>10} {'Confirmed':>15} {'Attempt':>15}")
    for i in range(MAX_POSITIONS):
        if asset_ids[i] == 0 and confirmed[i] == 0 and attempt[i] == 0:
            continue  # skip unused slots for readability
        print(f"{asset_ids[i]:>10} {confirmed[i]:>15} {attempt[i]:>15}")

    return {
        "status": status_str,
        "sequence_id": sequence_id,
        "client_id": client_id,
        "client_fd": client_fd,
        "asset_id": list(asset_ids),
        "confirmed": list(confirmed),
        "attempt": list(attempt),
    }


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <host> <port> [gateway_id] [client_id] [key_hex]")
        print("  gateway_id: defaults to 1")
        print("  client_id:  defaults to 0")
        print("  key_hex:    64 hex chars (32 bytes), the RAW secret whose SHA-256")
        print("              matches the target client's stored key hash.")
        print("              Defaults to the shared test key used throughout this project.")
        sys.exit(1)

    host = sys.argv[1]
    port = int(sys.argv[2])
    gateway_id = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    client_id = int(sys.argv[4]) if len(sys.argv) > 4 else 0

    key_hex = sys.argv[5] if len(sys.argv) > 5 else "aa92e9c3316ddc46dc882b45fe9f07b58230e83f3e3290ffbb5e2d6fa80ebd4f"
    key = bytes.fromhex(key_hex)

    client_fd = 0  # per the comment in ClientRequest.hpp, this is filled in by
    # the gateway itself server-side; sending 0 here as a placeholder

    request = build_request(
        request_type=b"A",
        client_id=client_id,
        key=key,
        client_fd=client_fd,
        gateway_id=gateway_id,
    )

    print(f"Connecting to {host}:{port} ...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((host, port))

        gateway_connection = build_gateway_connection(gateway_id)
        print(
            f"Sending {len(gateway_connection)}-byte GatewayConnection "
            f"(gateway_id={gateway_id})"
        )
        sock.sendall(gateway_connection)

        print(f"Sending {len(request)}-byte ClientRequest (type='A', client_id={client_id})")
        sock.sendall(request)

        data = b""
        while len(data) < RESPONSE_SIZE:
            chunk = sock.recv(RESPONSE_SIZE - len(data))
            if not chunk:
                print(
                    f"Connection closed after {len(data)}/{RESPONSE_SIZE} bytes "
                    f"-- incomplete response."
                )
                return
            data += chunk

        parse_response(data)


if __name__ == "__main__":
    main()