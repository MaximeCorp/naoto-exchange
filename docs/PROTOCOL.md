# Wire protocol

Byte-level specification of every message exchanged between Naoto Exchange
components. The source of truth is the struct definitions in
`market_server/common/include/` and `market_server/order_gateway/include/`;
the dashboard keeps its own copy in `market_client/include/wire_formats.hpp`
with `static_assert`s on every size.

For how these messages fit into the order lifecycle, see
[ARCHITECTURE.md § 2](ARCHITECTURE.md#2-order-lifecycle).

---

## 1. Conventions

| Rule | Detail |
|---|---|
| Byte order | **Little-endian** (x86-64 host order). Structs are `memcpy`'d onto the wire, never byte-swapped. Only the IP/UDP headers built for multicast are big-endian. |
| Packing | `#pragma pack(1)`. No implicit padding. Explicit `Padding` fields are zero. |
| Framing (TCP) | None. Each connection carries one fixed-size message type per direction, back to back. The receiver divides the byte stream by `sizeof(message)`. |
| Framing (multicast) | One UDP datagram holds 1 to 16 messages of a single type, concatenated. There is no packet header. |
| Integers | Signed where a value can be a delta (`int64_t` balances, prices), unsigned otherwise. |
| Prices and quantities | Plain integers, no implied decimals. Cash amounts are `Amount × Price`. |
| Asset 0 | The settlement currency. Every engine trades one asset against asset 0. |

### Build-dependent layouts

Building with `-DNAOTO_PERF=ON` inserts RDTSC timestamp fields into three
messages. **Both ends of a connection must be built with the same setting**,
otherwise the receiver splits the stream at the wrong offsets and every
message after the first is garbage.

| Message | Size, default | Size, `NAOTO_PERF` |
|---|---|---|
| `Order` | 40 | 64 |
| `OrderStateReport` | 48 | 80 |
| `OrderBookUpdate` | 19 | 27 |

All other messages have one layout.

---

## 2. Sessions at a glance

| Connection | Port | Initiator sends first | Then initiator sends | Acceptor sends |
|---|---|---|---|---|
| Client → gateway | TCP 8081 | `ClientAuthRequest` (37) | `Order` (40/64), repeated | `OrderConfirmation` (9), one per order |
| Gateway → account service | TCP 8082 | `GatewayHandshake` (2) | `RoutedAuthRequest` (43), repeated | `ClientAccountSnapshot` (305), one per request |
| Gateway → matching engine | TCP 8080 | nothing | `Order` (40/64), repeated | nothing |
| Gateway → matching engine (`NAOTO_SHARED_MEMORY`) | shm `/me_gw_shm` | n/a | `Order` by value into an SPSC ring | n/a |
| Engine → all | UDP 239.1.1.1:30001 | n/a | `OrderStateReport` (48/80) | n/a |
| Engine → all | UDP 239.1.1.2:30002 | n/a | `OrderBookUpdate` (19/27) | n/a |

Multicast source is `10.0.0.20:30000`, TTL 1, UDP checksum 0.

---

## 3. Client ↔ gateway

### 3.1 ClientAuthRequest (client → gateway, 37 bytes)

Must be the first message on every connection. A connection that has not
authenticated can still send orders; they are rejected `UserNotConnected`.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 1 | `char` | `RequestType` | `'A'` authenticate. (`'D'` is defined but not handled.) |
| 1 | 4 | `uint32` | `ClientId` | |
| 5 | 32 | `uint8[32]` | `Key` | Raw API key. The account service stores and compares `SHA-256(Key)`. |

The gateway does not answer this message. See
[ARCHITECTURE.md § 2.1](ARCHITECTURE.md#21-login).

### 3.2 Order (client → gateway, gateway → engine, 40 bytes)

Default layout:

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 8 | `int64` | `Price` | Limit price. Ignored for matching on `MARKET` sells; see note on market buys below. |
| 8 | 8 | `uint64` | `OrderId` | Set by the gateway. Clients send 0. |
| 16 | 4 | `uint32` | `ClientOrderId` | Client's own reference, echoed in the confirmation. |
| 20 | 4 | `uint32` | `ClientId` | Must match the authenticated client, else `BadClientId`. |
| 24 | 4 | `uint32` | `Amount` | Quantity. **For `CANCEL`, the `OrderId` of the order to cancel.** |
| 28 | 2 | `uint16` | `AssetId` | Selects the matching engine. |
| 30 | 1 | `uint8` | `Type` | `0` LIMIT, `1` MARKET |
| 31 | 1 | `uint8` | `Side` | `0` BUY, `1` SELL |
| 32 | 1 | `uint8` | `Action` | `0` EXECUTE (new order), `1` CANCEL |
| 33 | 7 | `uint8[7]` | `Padding` | Zero |

With `NAOTO_PERF`, three `uint64` fields are inserted after `Price`, and
everything from `OrderId` on moves down by 24 bytes:

| Offset | Size | Field | Stamped by |
|---|---|---|---|
| 8 | 8 | `IngestedTimestamp` | Gateway, when the batch is read from the socket |
| 16 | 8 | `RoutedTimestamp` | Gateway, after risk, just before forwarding |
| 24 | 8 | `ReceivedTimestamp` | Engine, when the order is dequeued |

Clients send these as 0.

**Cancels.** Set `Action = 1`, put the target `OrderId` (as returned in its
confirmation) in `Amount`, and set `Side` to the side of the resting order.
`Side` selects which book is searched. `ClientId` must own the target order,
otherwise the engine emits a `REJECT` report.

**Market buys.** A `MARKET` `BUY` with `Price = 0` never fills: the engine
stops sweeping when the best ask is above `Price`. Send a large sentinel
price. The gateway risk-checks market buys at `Amount × Price` too, so the
client needs that much cash available.

### 3.3 OrderConfirmation (gateway → client, 9 bytes)

Exactly one per `Order` received.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 4 | `uint32` | `OrderId` | Exchange order ID. Use it to cancel. |
| 4 | 4 | `uint32` | `ClientOrderId` | Echoed from the order. |
| 8 | 1 | `uint8` | `Status` | See table |

| Value | Status | Raised by |
|---|---|---|
| 0 | `Accepted` | Passed risk and forwarded to the engine. Not a fill. |
| 1 | `InsufficientFunds` | `amount > Confirmed − Attempt − LocalAttempt` |
| 2 | `MaxPositions` | Client has no position slot for the asset being sold (or cash, for buys). |
| 3 | `InvalidPrice` | Defined, not currently raised. |
| 4 | `InvalidQuantity` | Defined, not currently raised. |
| 5 | `UnknownSymbol` | Sell with `AssetId >= MaxAssets` (128). Buys are checked against cash only, so an out-of-range `AssetId` on a buy is not caught here. |
| 6 | `TechnicalFailure` | No live matching engine for this asset (TCP mode). |
| 7 | `UserNotConnected` | Connection has not completed login. |
| 8 | `BadClientId` | `Order.ClientId` differs from the authenticated client. |

---

## 4. Gateway ↔ account service

### 4.1 GatewayHandshake (gateway → account service, 2 bytes)

First message on the connection. Maps this socket to a gateway ID so replies
can be routed.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 2 | `uint16` | `GatewayId` | From the gateway's `MACHINE_ID`. Must be below `MaxGateways` (16). |

### 4.2 RoutedAuthRequest (gateway → account service, 43 bytes)

A `ClientAuthRequest` with routing information appended.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 37 | | | Same bytes as `ClientAuthRequest` |
| 37 | 4 | `uint32` | `ClientFd` | Gateway's socket fd for this client. Echoed back so the gateway knows which session the reply is for. |
| 41 | 2 | `uint16` | `GatewayId` | From the gateway's `GATEWAY_ID`. The reply is sent to the connection registered under this ID. |

The request is accepted when the client's stored authorised gateway equals
`GatewayId` **and** the key hash matches. `GatewayId = 10` bypasses the
gateway check; the dashboard uses it to query balances (temporary, marked
`TODO` in `client_request_processor.cpp`).

### 4.3 ClientAccountSnapshot (account service → gateway, 305 bytes)

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 1 | `char` | `Status` | `'A'` accepted, `'C'` wrong credentials, `'R'` refused (gateway not authorised) |
| 1 | 8 | `uint64` | `SequenceId` | Trade-report sequence the snapshot is current up to. **Currently always 0.** |
| 9 | 4 | `uint32` | `ClientId` | |
| 13 | 4 | `uint32` | `ClientFd` | Echoed from the request |
| 17 | 32 | `uint16[16]` | `AssetId` | Position slots |
| 49 | 128 | `int64[16]` | `Confirmed` | Balance per slot |
| 177 | 128 | `int64[16]` | `Attempt` | Reserved per slot |

On `'C'` and `'R'` all arrays are zero. Multi-byte fields after `Status` are
unaligned; read them with `memcpy`, not by casting a pointer.

---

## 5. Multicast feeds

### 5.1 Packet layout

```
 0                14               34       42
 ┌────────────────┬────────────────┬────────┬─────────┬─────────┬─────┬─────────┐
 │ Ethernet (14)  │ IPv4 (20)      │ UDP (8)│ msg[0]  │ msg[1]  │ ... │ msg[n-1]│
 │ dst = mcast MAC│ TTL 1, no opts │ csum 0 │         │         │     │  n ≤ 16 │
 └────────────────┴────────────────┴────────┴─────────┴─────────┴─────┴─────────┘
```

A receiver reads `udp_len − 8` payload bytes and steps through them in
`sizeof(message)` increments. Kernel-socket receivers (like the dashboard)
see only the UDP payload.

### 5.2 OrderStateReport (239.1.1.1:30001, 48 bytes)

One report per client per event. A match produces two: one for the
aggressor, one for the resting order.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 8 | `int64` | `BoughtDelta` | Change to `Confirmed` of `BoughtAssetId` |
| 8 | 8 | `int64` | `SoldDelta` | Change to `Confirmed` of `SoldAssetId` (≤ 0) |
| 16 | 8 | `int64` | `SoldAttemptDelta` | Change to `Attempt` of `SoldAssetId`. Positive on ADD (reserve), negative on fills and cancels (release). |
| 24 | 4 | `uint32` | `SequenceId` | Channel sequence number |
| 28 | 4 | `uint32` | `ClientId` | Whose balance this applies to |
| 32 | 4 | `uint32` | `OrderId` | Exchange order ID |
| 36 | 4 | `uint32` | `TradeId` | Always 0. Cannot be used to pair the two legs. |
| 40 | 2 | `uint16` | `BoughtAssetId` | |
| 42 | 2 | `uint16` | `SoldAssetId` | |
| 44 | 4 | `uint32` | `State` | See table |

With `NAOTO_PERF`, four `uint64` fields are inserted at offset 24 (before
`SequenceId`), and everything after moves down by 32 bytes:
`IngestedTimestamp`, `RoutedTimestamp`, `ReceivedTimestamp` (copied from the
order) and `UpdateTimestamp` (engine, when this report was built; 0 on the
resting order's leg of a fill).

| Value | State | Meaning |
|---|---|---|
| 0 | `FILL` | Order fully filled by this match |
| 1 | `PARTIAL_FILL` | Order partly filled, remainder still live |
| 2 | `CANCEL` | Remainder removed: client cancel, or unfilled part of a `MARKET` order |
| 3 | `REJECT` | Cancel request for an unknown order, or one owned by another client |
| 4 | `ADD` | Order now rests in the book |

**Reading a trade.** On a buyer's report, `BoughtAssetId` is the traded asset
and `SoldAssetId` is 0; price is `−SoldDelta / BoughtDelta`. See the worked
example in [ARCHITECTURE.md § 4.2](ARCHITECTURE.md#42-how-every-report-changes-a-balance).

### 5.3 OrderBookUpdate (239.1.1.2:30002, 19 bytes)

Level-2, price-level updates with absolute quantities. One update per book
change.

| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 8 | `int64` | `Price` | Price level affected |
| 8 | 4 | `uint32` | `SequenceId` | Channel sequence number |
| 12 | 4 | `uint32` | `Depth` | **Total** resting quantity at this level after the event. 0 means the level is gone. |
| 16 | 2 | `uint16` | `AssetId` | |
| 18 | 1 | `uint8` | `Side` | `0` bid, `1` ask |

With `NAOTO_PERF`, a `uint64 UpdateTimestamp` is prepended at offset 0 and
everything else moves down by 8 bytes.

To maintain a book: `book[Side][Price] = Depth`, erase when `Depth == 0`.

### 5.4 Sequence numbers

- Each channel counts independently from 0 at engine start, +1 per message.
- Set by the emitter, not the matching thread. The `REPORT_SEQUENCE_ID` and
  `ORDER_BOOK_SEQUENCE_ID` environment variables seed the matching thread's
  counters, but those values are overwritten before transmission and never
  reach the wire.
- A gap means a lost message. There is currently no retransmission or
  snapshot service to recover it (see
  [ARCHITECTURE.md § 7](ARCHITECTURE.md#7-sequence-numbers-and-recovery)).
- The counter is 32-bit and wraps.

---

## 6. Changing a message

1. Edit the struct in `market_server/common/include/` (or
   `order_gateway/include/` for `OrderConfirmation`).
2. Update `market_client/include/wire_formats.hpp` and its `static_assert`s.
3. Update the tables in this file.
4. Update `market_server/functional_tests/realistic_stress_test.cpp` if it
   redefines the struct locally (it does for `ClientRequest`). The Python
   helpers in the same folder (`order_defs.py`, `send_orders.py`,
   `stress_test.py`) still describe an **older** `Order` layout and do not
   match the current wire format.
5. Rebuild **every** binary. There is no versioning on the wire, so a stale
   binary will not fail loudly; it will misparse.
