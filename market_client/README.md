# Trading Dashboard

Dear ImGui monitoring and manual-trading UI for Naoto Exchange. It shows:

- services discovered in etcd,
- live order books, rebuilt from the book-update multicast,
- the execution-report feed and price history,
- client balances, queried from the account service,
- two independently authenticated trading panels, so you can trade as two
  clients against each other,
- latency statistics from `NAOTO_PERF` timestamps
  (see [docs/LATENCY.md](../docs/LATENCY.md)).

The dashboard is a client of the exchange like any other. It uses kernel
sockets only and needs no DPDK.

## Build and run

The dashboard is built as part of the root CMake project:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release        # from the repo root
cmake --build build -j --target trading_dashboard
MCAST_IFACE=veth1 ./build/market_client/trading_dashboard
```

Requires GLFW3 and OpenGL development packages, plus the etcd-cpp-apiv3,
gRPC, protobuf and cpprest libraries the rest of the project uses. ImGui,
ImPlot and nlohmann/json are fetched with `FetchContent` at configure time;
set `-DLOCAL_DEPS_DIR=<dir>` to use local clones instead.

**Build it from the same build tree, with the same `NAOTO_PERF` setting, as
the services.** `NAOTO_PERF` changes the wire size of `Order`,
`OrderStateReport` and `OrderBookUpdate`. A mismatch does not fail loudly: the
feeds decode into nonsense.

## Before you start: etcd key for balance queries

Balance refresh and the client lookup panel only send a query once an entry
exists under `/client-details-provider/` in etcd. The account service
registers itself as `/account-service/0` instead, so out of the box those
buttons do nothing. Until the two are aligned, add the key by hand:

```bash
etcdctl put /client-details-provider/0 '{"addr":"127.0.0.1:8082","status":"active"}'
```

Trading itself does not depend on this.

## Source layout

```
include/
  wire_formats.hpp          packed copies of the server's wire structs, with size static_asserts
  mpsc_queue.hpp            mutex-based queue, network threads -> render thread
  multicast_receiver.hpp    kernel UDP multicast socket
  trading_session.hpp       one TCP connection = one authenticated client session
  client_details_poller.hpp one-shot balance query to the account service
  etcd_watcher.hpp          watches /matching-engines/, /socket-gateways/, /client-details-provider/
  app_state.hpp             render-thread-owned state: books, feeds, balances, price history
  log.hpp                   DASHBOARD_LOG macro
src/
  main.cpp                  ImGui app, wires everything together, owns the render loop
```

`wire_formats.hpp` is a copy, not an include, of the server structs. When a
server message changes, update it too ([docs/PROTOCOL.md § 6](../docs/PROTOCOL.md#6-changing-a-message)).

## Threading model

One thread per data source, each feeding the render thread through an
`MpscQueue<T>`:

```
  etcd watcher (pplx pool) ───────┐
  MulticastReceiver: reports ─────┤
  MulticastReceiver: book ────────┼──▶ MpscQueues ──▶ render thread ──▶ AppState, ImGui
  TradingSession A reader ────────┤                   (drains every queue
  TradingSession B reader ────────┤                    once per frame, before
  balance queries (detached) ─────┘                    any ImGui call)
```

- **Render thread** owns every ImGui call and all of `AppState`. No other
  thread touches either.
- **etcd watcher** callbacks run on cpprestsdk's `pplx` thread pool, which
  the dashboard does not control.
- **Multicast receivers**, one per feed, each on its own thread.
- **Trading sessions** each have a reader thread for `OrderConfirmation`s.
  Orders are sent directly from the render thread; `send()` on a connected
  `TCP_NODELAY` socket returns quickly. Moving sends to a writer thread would
  mean adding an outbound `MpscQueue<Order>`.
- **Balance queries** (`ClientDetailsPoller::query`) are blocking one-shots.
  Each call site runs one on a detached `std::thread` and pushes the result
  into a queue.

## Two connection types

The dashboard opens two kinds of TCP connection, to two different services.
They are never mixed.

| | Trading connection | Balance query connection |
|---|---|---|
| Code | `trading_session.hpp` | `client_details_poller.hpp` |
| Connects to | `order_gateway`, address from `/socket-gateways/<id>` | `account_service`, address from `/client-details-provider/<id>` (see above) |
| Lifetime | Long-lived, one per trading panel | Short-lived, one per query |
| Sends | `ClientAuthRequest` (37 B), then `Order`s | `GatewayHandshake` (2 B), then one `RoutedAuthRequest` (43 B) |
| Receives | `OrderConfirmation` (9 B) per order | One `ClientAccountSnapshot` (305 B) |

The balance query works by **impersonating a gateway**. It sends the handshake
and request a real gateway would send, with gateway ID 10. The account
service lets gateway 10 through regardless of which gateway the client is
assigned to (a temporary bypass marked `TODO` in
`account_service/src/client_request_processor.cpp`). Using 10 rather than the
real gateway's ID 0 matters: the account service keeps one connection per
gateway ID, so a dashboard claiming to be gateway 0 would take over the real
gateway's slot and its login replies would come to the dashboard instead.

`ClientFd` in the request is sent as 0, since the dashboard has no real
client fd. The account service only echoes it back.

Message layouts are in [docs/PROTOCOL.md](../docs/PROTOCOL.md).

## etcd discovery

What the services actually register:

```
/matching-engines/1      {"addr":"127.0.0.1:8080","asset_id":1,"status":"active"}
/socket-gateways/0       {"ip":"localhost:8081","status":"active"}
/account-service/0       {"addr":"127.0.0.1:8082","status":"active"}
```

`socket-gateways` uses the key `"ip"` where the others use `"addr"`. That is
the real field name.

The trading panels and the lookup panel offer a "pick a discovered endpoint"
list, so host and port do not have to be typed in. If nothing has been
discovered, the trading panel falls back to `config::kTradingGatewayHost` /
`kTradingGatewayPort` in `main.cpp`, which is **127.0.0.1:8000**. The gateway
listens on 8081, so either pick the discovered entry or change the port.

`parse_host_port()` maps `localhost` to `127.0.0.1` instead of resolving it.
Fine for a single host, needs `getaddrinfo` for anything else.

The numeric suffix of `/socket-gateways/<id>` is the gateway's `MACHINE_ID`.

## Login timing

After sending `ClientAuthRequest`, `TradingSession::connect()` sleeps 100 ms
before marking the session `Connected` and allowing orders. The gateway sends
no login acknowledgement, so this is a guess at how long the round trip to
the account service takes, not a guarantee. On a local setup the round trip
is far shorter than 100 ms.

If the first orders after connecting come back `UserNotConnected`, the login
has not landed yet, or has failed. The real fix is a login ack from the
gateway.

## Receiving multicast on the same host

The engine sends with DPDK, but the dashboard receives on an ordinary kernel
socket, so the usual kernel multicast rules apply to it:

1. **Interface.** Join the groups on the interface the traffic arrives on.
   In the veth test setup that is `veth1`. Set it with `MCAST_IFACE=veth1`
   at runtime, or `config::kMcastLocalInterface` at compile time. Left empty,
   the kernel picks an interface, which is often the wrong one.
2. **Reverse-path filtering.** Linux can drop a multicast packet whose
   source address looks wrong for the receiving interface. The setup script
   disables it; to do it by hand:

   ```bash
   sudo sysctl -w net.ipv4.conf.all.rp_filter=0
   sudo sysctl -w net.ipv4.conf.veth1.rp_filter=0
   ```

For a test with no veth pair, `MCAST_IFACE=lo` works if the sender also sets
`IP_MULTICAST_IF` to 127.0.0.1.

Feed groups are fixed in the engine's `main.cpp`: reports on
`239.1.1.1:30001`, book updates on `239.1.1.2:30002`. All engines publish on
the same two groups, so one receiver pair sees every asset; updates are told
apart by `AssetId`.

## Balances

Balance queries are request/response; there is no push. The dashboard keeps
balances current in two ways:

1. **Optimistic update.** Every `OrderStateReport` for a tracked client (a
   trading panel's client, or a looked-up one) is applied to the local
   balance as it arrives (`AppState::apply_trade_to_balance`).
2. **Reconciliation on demand.** "Refresh Balance" on a trading panel, or
   "Query" on the lookup panel, asks the account service on a short-lived
   connection, separate from the trading session so a slow query can never
   block an order. The answer overwrites the local state.

There is no periodic poll. If a report is lost, the displayed balance stays
wrong until the next manual refresh.

## Price history

Built entirely from the two multicast feeds, with no historical-data
service:

- **Trades** come from `FILL` / `PARTIAL_FILL` reports. Each match produces
  one report per side and neither carries a price, so only the buyer's leg is
  used (`BoughtAssetId != 0`, `SoldAssetId == 0`), with
  `price = −SoldDelta / BoughtDelta`. `TradeId` is always 0, so it cannot be
  used to pair legs.
- **Best bid and ask** are sampled whenever a book update moves the top of
  book.
- **Time** is the dashboard's own wall-clock receive time, stamped on the
  network thread. Wire timestamps are raw TSC cycles and exist only with
  `NAOTO_PERF`, so they cannot be used as a clock axis.

Candles are rebuilt from the raw trade list every frame, so changing the
timeframe re-buckets the whole session. The page footer shows raw counters
(book updates, order reports, decoded trades), which tell a dead feed apart
from a quiet market.

## Open questions

1. **Which gateway a client is authorised on.** All demo clients are
   assigned to gateway 0. The panels let you override the gateway ID per
   query rather than assuming; revisit once there is more than one gateway.
2. **Market data for late joiners.** There is no book snapshot, so a
   dashboard started after the engine only sees levels that change after it
   joins. The retransmitter will address this.
