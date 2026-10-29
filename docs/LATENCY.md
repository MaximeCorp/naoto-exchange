# Latency measurement

What the latency numbers in the README measure, how they are taken, and what
they leave out. Read § 4 before quoting a number.

Component microbenchmarks (price-level map, core-to-core handoff, client
state reads) are a separate exercise, written up in
[BENCHMARKS.md](BENCHMARKS.md).

---

## 1. Measurement points

Build with `-DNAOTO_PERF=ON`. This adds four TSC timestamps that travel with
each order and come back out on its `OrderStateReport`
(layouts in [PROTOCOL.md](PROTOCOL.md#build-dependent-layouts)).

```
 client        order_gateway                        matching_engine                     wire
   │      ┌────────────────────────┐      ┌──────────────────────────────┐
   │ TCP  │ recv()   risk check    │ SPSC │ dequeue   match   build      │ SPSC   DPDK TX
   ├─────▶│   │          │         ├─────▶│   │         │     report     ├──────▶ ───────▶
   │      │   T1         T2        │ or   │   T3              T4         │ emitter
   │      └───┼──────────┼─────────┘ TCP  └───┼───────────────┼──────────┘
   │          │          │                    │               │
   │          Ingested   Routed               Received        Update
   │
   │          ├─ gateway ┤├── handoff ────────┤├── matching ──┤
   │          ├──────────────────── total (T4 − T1) ──────────┤
```

| Stamp | Field | Where in the code | Taken when |
|---|---|---|---|
| T1 | `IngestedTimestamp` | `GatewayServer::BatchHandle` | Batch of orders has been read from the client socket. One value for the whole batch. |
| T2 | `RoutedTimestamp` | `OrderRouter::consumeOrder` | Risk check passed, immediately before the order is pushed to the engine. |
| T3 | `ReceivedTimestamp` | `BidAsk::MarketExecutionLoop` (shm mode); `OrderIngressServer::BatchHandle` (TCP mode) | Shm: matching thread has popped the order. TCP: engine's epoll thread has read it from the socket. |
| T4 | `UpdateTimestamp` | `BidAsk`, each `FillReport` call | The report for this event has been filled in, before it is queued for the emitter. |

All four use `now_tsc()` (`lfence; rdtsc; lfence`) from
`common/include/timestamps.hpp`.

## 2. Spans

The dashboard's latency page computes, per report:

| Span | Formula | README name |
|---|---|---|
| Gateway internal | T2 − T1 | "Gateway internal (risk check + ack)" |
| Gateway → engine | T3 − T2 | "Shared-memory SPSC handoff to matching" |
| Engine → execution | T4 − T3 | "Matching" |
| Total | T4 − T1 | "Tick to trade" |

Each is reported as min / P50 / mean / P95 / P99 / max, using linear
interpolation between ranks, over the last 200,000 reports the dashboard has
received.

---

## 3. Reproducing the headline numbers

Reference result (root README): **P99 600 ns matching, P99 1.1 µs gateway
internal, at 40k orders/s.**

1. Machine prep, before anything else:
   - Isolate the hot cores: `isolcpus=0-9 nohz_full=0-9 rcu_nocbs=0-9` on the
     kernel command line.
   - Pin IRQs off them (reference: cores 10-11).
   - Limit C-states (reference: max C1) and use a fixed frequency governor.
2. Build: `cmake -B build -DNAOTO_PERF=ON -DNAOTO_SHARED_MEMORY=ON -DCMAKE_BUILD_TYPE=Release`.
   `NAOTO_SANITIZE` must stay `OFF`.
3. Start the services ([OPERATIONS.md § 4](OPERATIONS.md#4-startup)), with
   stdout redirected away from a terminal.
4. Run the load: `./build/realistic_stress_test --rate 40000 --duration 60`
   (built with `-DNAOTO_PERF`, see [OPERATIONS.md § 6](OPERATIONS.md#6-load-generation)).
5. Open the dashboard's latency page and **set "ns per RDTSC cycle"**. The
   default is 1.0, which is only right for a 1 GHz TSC; until you change it,
   every number on the page is in cycles, not nanoseconds. Find the TSC rate
   with:

   ```bash
   sudo dmesg | grep -i 'tsc.*MHz'
   # tsc: Refined TSC clocksource calibration: 2994.384 MHz
   #   -> ns per cycle = 1000 / 2994.384 = 0.334
   ```

6. Set the resting-order threshold so orders that sat in the book are
   excluded (see § 4).

---

## 4. Reading the numbers critically

These are **internal, software-timestamped** latencies. They are useful for
comparing changes to the code. They are not comparable to a venue's
published wire-to-wire figures. Specifically:

**What the total does not include**

- Anything before the gateway's `recv()` returns: NIC, kernel TCP stack,
  time spent waiting in the socket buffer. If the gateway's epoll thread
  falls behind, orders queue in the kernel and **none of that wait shows up**.
  This is the main way these numbers can look better than what a client
  experiences.
- Anything after the report is built: the emitter queue, packet build, DPDK
  TX, the wire, and the subscriber's receive path.
- The ack path to the client. The ack is sent by a different thread
  (`ClientMessageSender`) and is not timestamped. Despite the README's
  "risk check + ack" label, T2 − T1 ends before the ack is queued.

**Naming.** "Tick to trade" conventionally means market-data packet in to
order packet out of a trading system. Here "total" is order read by the
gateway to execution report built by the engine. "Internal order-to-report"
is a more accurate name.

**TCP mode moves time between spans.** In TCP mode T3 is taken by the
engine's epoll thread, not the matching thread, so the wait in the engine's
internal queue counts as "matching", not "handoff".

**Resting orders.** T1 to T3 are stamped once when an order arrives. If it
rests and fills a minute later, its report has a one-minute "matching" time.
The resting order's leg of a fill carries T4 = 0. Both must be filtered out
before computing percentiles; the dashboard's resting-order threshold does
the first.

**Sample weighting.** Statistics are per report, not per order. A taker that
sweeps five levels contributes five reports with identical T1 to T3, so
aggressive orders are over-represented relative to their count.

**Clock domain.** RDTSC values from different processes are only comparable
because they run on the same host with an invariant TSC (`constant_tsc` and
`nonstop_tsc` in `/proc/cpuinfo`). Timestamps from two hosts cannot be
subtracted. A negative span on the dashboard means one of those assumptions
is broken.

**Measurement cost.** Each `now_tsc()` is two serialising fences plus
`rdtsc`, a few tens of cycles. With four stamps per order that is not
negligible against a 600 ns span, and it is included in the result.

**Configuration matters more than code.** The bench results show a busy
thread sharing a core with two other spinners turns a 0.3 µs handoff into
1.5 ms. Check thread placement (`ps -L -o tid,psr,comm`) before attributing a
regression to a code change.

---

## 5. Suggested improvements

Recorded here because they change what the numbers mean:

- Use `SO_TIMESTAMPING` on the gateway's client socket (or NIC hardware
  timestamps) to capture the kernel-side wait before T1.
- Stamp the ack when `ClientMessageSender` writes it, to measure true
  order-to-ack.
- Stamp T3 on the matching thread in TCP mode too, so spans mean the same in
  both builds.
- Export raw samples to a file (HDR histogram or CSV) so results can be
  compared across runs without the dashboard.
