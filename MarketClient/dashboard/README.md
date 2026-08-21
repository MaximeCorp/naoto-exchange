# Trading Dashboard (Dear ImGui)

Read-only-except-for-trading monitoring dashboard for the matching engine
system: etcd resource view, live order books, live trade tape, on-demand
client balance lookup, and two independently-authenticated trading
panels for trading as two clients simultaneously.

## Layout

```
include/
  wire_formats.hpp          exact packed structs, matches the given binaries
  mpsc_queue.hpp             mutex-based queue for network-thread -> render-thread handoff
  multicast_receiver.hpp     kernel UDP multicast socket (no DPDK needed here)
  trading_session.hpp        one TCP connection = one authenticated client session
  client_details_poller.hpp  one-shot balance query (used for polling + lookup panel)
  etcd_watcher.hpp           /matching-engines/ prefix watch, raw k/v for now
  app_state.hpp              render-thread-owned aggregate state (order books, feed, balances)
src/
  main.cpp                   ImGui app, wires everything together, owns the render loop
CMakeLists.txt
```

## Threading model

One thread per data source, all feeding the render thread through
`MpscQueue<T>`:

- Render/main thread: owns every ImGui call and all of `AppState`. Nothing
  else is allowed to touch either.
- etcd watcher: `etcd::Watcher` callback runs on cpprestsdk's `pplx`
  thread pool (not a thread we control) -> pushes `EtcdEvent`s.
- Two `MulticastReceiver` instances (order state reports, book updates),
  each with their own background thread -> push parsed structs.
- Each `TradingSession` has its own background reader thread for
  `OrderConfirmation`s, and its own TCP socket for sending `Order`s (send
  side isn't threaded separately since orders are placed one at a time
  from user input on the render thread — the `send()` call itself is
  quick/non-blocking in practice, but if you want it fully off-thread
  too, that's a small follow-up: add an outbound `MpscQueue<Order>`
  drained by a dedicated writer thread).
- `ClientDetailsPoller::query()` is a blocking one-shot; every call site
  wraps it in a detached `std::thread` and pushes the result back into a
  queue.

Every queue is drained once per frame, at the top of the render loop,
before any ImGui calls.

## Two separate protocols — don't conflate them

There are two independent TCP connection types in this system:

1. **Trading connection**: dashboard → socket-gateway (address from etcd
   `/socket-gateways/<id>`). Handshake is `Gateways::ClientRequest{'A',
   ClientId, Key}` (37 bytes), then `Order` structs, receiving
   `OrderConfirmation`s back. Handled by `trading_session.hpp`.
2. **CDP connection**: dashboard → CDP directly (address from etcd
   `/client-details-provider/<id>`), impersonating the client's assigned
   gateway. Handshake is `GatewayConnection{GatewayId}` (2 bytes, always
   first), then `GatewayRequest{'A', ClientId, Key, ClientFd, GatewayId}`
   (43 bytes), receiving `ClientRequestResponse<MaxPositions>` back.
   Handled by `client_details_poller.hpp`. This is how the dashboard gets
   balances without going through the REST API a normal client would use
   — confirmed as the intended approach.

These are separate sockets to separate services; a `TradingSession` never
talks to CDP and a `ClientDetailsPoller` query never talks to a
socket-gateway.

## etcd schema (confirmed, real sample)

```
/matching-engines/1            {"addr":"127.0.0.1:8080","asset_id":1,"status":"active"}
/client-details-provider/0     {"addr":"127.0.0.1:8082","status":"active"}
/socket-gateways/0              {"ip":"localhost:8000","status":"active"}
```

`etcd_watcher.hpp` watches all three prefixes and parses each into
`MatchingEngineInfo` / `AddrStatusInfo`, held in `AppState`. The trading
panels and the client-lookup panel both offer a "pick a discovered
gateway/CDP" flow instead of requiring the host/port to be typed in by
hand — see `draw_trading_panel` / `draw_client_lookup_panel` in
`main.cpp`. Note `socket-gateways` uses the key `"ip"` where the other
two use `"addr"` — that's the real field name, not a typo carried over
from copy-paste.

The numeric suffix on a `/socket-gateways/<id>` key is assumed to be that
gateway's `GatewayId` (only one gateway running in the test setup, so
this is a plausible-but-unconfirmed convention rather than something
stated outright — re-verify once there's more than one).

## The 100ms wait before sending orders

`trading_session.hpp`'s `connect()` sleeps 100ms after sending the
initial `ClientRequest`, before flipping to `SessionState::Connected` (in
turn actually gating `send_order()`). This matches the gateway's
request-send thread polling two queues with a 75ms blocking timeout each
— there's no ack to wait on, so this is a deliberate "wait long enough
that the auth message is very likely to have been dequeued and applied"
buffer rather than a guarantee. If order placement right after connect
ever comes back `UserNotConnected`, that's the first thing to suspect —
worth widening the sleep or, better, getting an actual ack added
upstream.

## Same-host multicast note, corrected

Earlier I described the loopback caveat as if it were a DPDK issue — it
isn't. DPDK bypasses the kernel on the *sending* side, but **this
dashboard's receiver is a plain kernel socket regardless of how the
sender transmits**, so the usual same-host caveats still apply to it.
Two separate things to get right:

1. **Interface selection.** `multicast_receiver.hpp` now takes an
   optional `local_interface_name` (e.g. `"veth1"`) instead of always
   joining on `INADDR_ANY`. In a DPDK `net_af` veth-pair test setup
   (sender on `veth0`, traffic landing on `veth1`), letting the kernel
   guess the interface may mean the receiver never sees the traffic at
   all — set `config::kMcastLocalInterface` to the interface the traffic
   actually lands on.
2. **`rp_filter`.** Even joined on the right interface, Linux reverse-path
   filtering can still drop a multicast packet whose source looks "wrong"
   for that interface:
   ```
   sudo sysctl -w net.ipv4.conf.all.rp_filter=0
   sudo sysctl -w net.ipv4.conf.veth1.rp_filter=0   (or whichever
                                                      interface is used)
   ```

## Balance "real-time-ness"

The `ClientRequest`/response protocol is query-on-demand, not a
subscription — there is no server push for balance changes. This
dashboard fakes real-time-ness two ways, combined:

1. **Optimistic local update**: every `OrderStateReport` that arrives on
   the multicast feed for a client we're tracking (either trading panel,
   or a looked-up client) is applied immediately to that client's local
   balance (`AppState::apply_trade_to_balance`).
2. **Periodic reconciliation**: every ~1s, connected trading panels get a
   fresh `ClientRequest{'A', ...}` poll on a short-lived connection
   (separate from the trading session's long-lived one, so a balance
   refresh can never block order placement), which overwrites the local
   state with ground truth.

This means between polls the UI can be briefly wrong if a packet is
dropped/reordered, but self-corrects within ~1s. If CDP/the gateway ever
gets a genuine balance-change push/subscription, swap step 2 for that and
drop the polling loop.

## Known assumptions to confirm (flagged inline in code too)

1. **`MaxPositions` value for `ClientRequestResponse`** — the struct
   layout itself is confirmed correct, but `MaxPositions` is a
   compile-time template parameter that directly determines the
   response's wire size. Currently hardcoded to 16
   (`CDP_MAX_POSITIONS` in `client_details_poller.hpp`) — confirm
   against the deployed CDP config.
2. **Which `GatewayId` is authorized for a given client** — with a
   single test gateway (`/socket-gateways/0`) this is trivially 0 for
   everyone, so it's untested against a real multi-gateway assignment
   rule. The dashboard currently lets you set/override this per trading
   panel and per lookup query rather than assuming — revisit once
   there's more than one gateway to actually distinguish clients by.
3. **`ClientFd` sent in `GatewayRequest`** — the dashboard impersonates a
   gateway it isn't, so there's no real client file descriptor to
   report; currently sends `0`. Revisit if CDP uses this for anything
   beyond logging/correlation on the genuine gateway's side.
4. **Multicast group/port for order-state and book-update feeds** —
   still not in etcd and not otherwise given; `config::kOrderStateMcastGroup/Port`
   and `kOrderBookMcastGroup/Port` remain placeholders. If these turn out
   to be per-matching-engine (discovered via etcd like everything else)
   rather than fixed, the dashboard needs one `MulticastReceiver` pair
   per engine instead of one global pair.
5. **`Order.Timestamp` units** — unspecified upstream; sent as
   milliseconds since epoch, purely informational, not relied on for any
   logic here.
6. **"localhost" in etcd `ip`/`addr` fields** — `parse_host_port()` in
   `main.cpp` hardcodes `localhost` → `127.0.0.1` rather than doing real
   DNS resolution (`getaddrinfo`). Fine for this test setup; would need
   upgrading if a real deployment ever puts non-localhost hostnames in
   etcd.

## Build

Requires: a C++20 compiler, GLFW3 + OpenGL dev packages, and
`etcd-cpp-apiv3` + its dependencies (`grpc++`, `grpc`, `protobuf`,
`cpprest`) already installed/discoverable, matching the rest of this
project's build setup.

```
mkdir build && cd build
cmake ..
cmake --build . -j
./trading_dashboard
```

ImGui and GLFW's CMake integration are fetched via `FetchContent` from
GitHub at configure time; everything else is expected to already be on
the build machine per this project's existing conventions.
