# Component benchmarks

Microbenchmarks of three pieces of the hot path, run on 2026-10-02: the
price-level map, core-to-core handoff, and the gateway's client-state
buffer. They measure each component in isolation, against the production
headers, and complement the end-to-end numbers in [LATENCY.md](LATENCY.md).

The benchmark harness is not part of the repository. This page records the
method, the results and the two bugs the runs found, so the conclusions do
not depend on it.

## Environment

| | |
|---|---|
| CPU | AMD Ryzen 5 4600H (Zen 2), 6 cores / 12 threads |
| Topology | Two CCXs, CPUs 0-5 and 6-11, each with its own 4 MB L3. SMT pairs are (0,1), (2,3), … |
| OS | Linux 7.0, `schedutil` governor, boost on |
| Compiler | `g++ -std=c++20 -O3 -march=native`, same flags as the Release services |
| Timing | RDTSC, converted to ns against `steady_clock` once per run. Threads pinned with `pthread_setaffinity_np`. |
| Repeats | Every figure is the median of 3 runs unless stated |

The machine was **not** isolated for these runs (no `isolcpus`, boost on),
unlike the end-to-end measurements. Tails (P99 and beyond) are therefore
pessimistic; medians are comparable.

---

## 1. Price-level map

The matching engine finds a price level through `FlatHashMap<int64_t,
PriceLevel*>`, a Robin Hood table probed 16 slots at a time with SIMD. It is
compared here against a plain one-slot-at-a-time probe of the same table and
the usual standard containers, on the book's own key type and table size.

**Method.** Per-lookup P50, from batches of 32 dependent lookups (a single
lookup is below what RDTSC resolves). Run with the lane-0 fix below applied.

| Live levels | Naoto SIMD | Naoto scalar probe | `absl::flat_hash_map` | `std::unordered_map` | `std::map` |
|---|---|---|---|---|---|
| 64   | 9.7 ns  | 5.3 ns  | 9.7 ns  | 8.8 ns  | 25.4 ns |
| 1024 | 14.1 ns | 10.3 ns | 13.2 ns | 12.2 ns | 52.6 ns |
| 8192 | 15.3 ns | 19.7 ns | 14.1 ns | 21.3 ns | 79.8 ns |

Insert / erase P50 at 8192 levels: Naoto 18.5 / 29.4 ns, absl 16.0 / 12.5,
`unordered_map` 29.7 / 39.5, `std::map` 115.5 / 128.7. At 64 levels Naoto is
the fastest at both (3.8 / 2.5 ns, against absl's 5.9 / 6.6).

- With a full book the SIMD map matches absl, and is 1.4× faster than
  `std::unordered_map` and 5.2× faster than `std::map`.
- The SIMD probe only pays off once probe sequences get long. Below about
  1000 live levels the scalar probe over the same table is faster.
- `std::map`'s cost is its median, not its tail: insert P99 is about 1.2×
  its P50. Its P99.9 (~400 ns) also shows up on lookups, which never
  rebalance, so that tail is timer interrupts landing in its longer
  batches, not rebalancing.
- Erase is Naoto's weak spot at high fill: the backward shift moves real
  data.

### Bug: keys become unreachable at high fill

`FlatHashMap::AddNode` skips the Robin Hood swap when the displaced slot is
lane 0 of a 16-slot group (`market_server/common/include/flat_hash_map.hpp`,
line 237). Probe distances then grow past 127, where the `0x80` empty marker
and the signed SIMD compare stop working, and keys can no longer be found.

| State | Result with the current header |
|---|---|
| Up to 5120 live levels | Correct |
| ~6144 levels after churn | Keys lost |
| Filled to 8192 (`MeMaxPriceLevels`) | Keys lost immediately |
| 500k insert/erase steps at 8192 | All 8192 live keys unreachable |

The fix is one line:

```diff
--- a/market_server/common/include/flat_hash_map.hpp
+++ b/market_server/common/include/flat_hash_map.hpp
@@ -234,7 +234,7 @@
                                      val);
                     return;
                 }
-                else if (first_available)
+                else
                 {
                     cur_idx = slot_idx;
                     cur_dib += first_available;
```

With it, the maximum probe distance at 8192 levels drops from 134 to 18, and
2M churn steps pass with every key checked after each step. **Not yet applied
to the repository.**

---

## 2. Core-to-core handoff

What it costs to move data between cores on this machine, and what that
means for where the gateway's threads are pinned.

**Method.** One-way latency is half the round trip of two pinned threads
ping-ponging one cache line (each waits for the other's value, then writes
the next). CPU 0 is excluded as noisy.

| Pair | One way |
|---|---|
| Same core (SMT siblings) | 40 ns |
| Same CCX | 53 ns |
| Across CCXs | 90 ns |

The real `SpscQueue`, on the gateway's actual thread pairs, one way, P50:

| CPUs | Gateway role | P50 |
|---|---|---|
| 11 → 10 | trade reports → states writer (SMT siblings) | 50 ns |
| 2 → 4 | epoll server → risk (same CCX) | 90 ns |
| 10 → 4 | states writer → risk (across CCXs) | 180 ns |
| 4 → 10 | risk → ack sender (across CCXs) | 180 or 385 ns, bimodal across runs |

The 1.1 µs gateway figure in the README is ingest (epoll server, CPU 2) to
routed (risk, CPU 4). That pair is already on one CCX, so re-pinning would
not change it.

### Busy-polling threads sharing a core

What does matter: the gateway pins the states writer, the account-service
response receiver and the ack sender, all busy-polling, to CPU 10. A
consumer on CPU 10, fed one item every 20 µs from CPU 4 (open loop):

| CPU 10 | P50 | P99 |
|---|---|---|
| Consumer alone | 0.3 µs | 0.7 µs |
| Consumer + 2 spinning threads | 1.47 ms | 5.96 ms |

The scheduler time-slices the three spinners, so an item queued for one of
them waits for its slice. The stress test does not time acks on the client
side, so this effect on acks is not yet confirmed end to end.

---

## 3. Client state: read cost, staleness, torn reads

The risk thread reads a client's state on every order while the states
writer applies trade reports to it. See
[ARCHITECTURE.md § 4.4](ARCHITECTURE.md#44-triple-buffered-client-state)
for the triple buffer.

### Read cost

Risk-check read on CPU 4, writer on CPU 10, all 3 runs agree:

| Implementation | Writer at 1M updates/s: P50 / P99 | Writer flat out: P50 / P99 |
|---|---|---|
| Triple buffer, full copy (as today) | 60 / 150 ns | 120 / 140 ns |
| Triple buffer, one slot, no copy | 50 / 150 ns | 120 / 140 ns |
| Seqlock | 60 / 75 ns | 7,500 / 365,000 ns |
| `std::mutex` | 70 / 950 ns | 780 / 1,500 ns |

- At realistic rates a seqlock matches or beats the triple buffer. The
  triple buffer's advantage is that a read stays bounded whatever the
  writer does; a busy writer starves seqlock readers.
- Copying the whole 320-byte `ClientState` instead of one slot costs about
  10 ns.

### Staleness

Time from a trade report being queued to the risk thread seeing it, through
the real `ClientStatesWriter::StartLoop`, one report every 20 µs, 3 runs.
The two spinners stand in for the account receiver and the ack sender.

| Configuration | P50 | P99 |
|---|---|---|
| As in the repo: per-report prints (to a file), writer shares CPU 10 with 2 spinners | 1.7 ms | 6.0 ms |
| Prints, writer alone on CPU 10 | 1.37 µs | 7.0 µs |
| No prints, writer shares CPU 10 | 1.5 ms | 6.0 ms |
| No prints, writer alone on CPU 10 (other CCX from risk) | 0.52-0.71 µs | 0.95 µs |
| No prints, receiver on 2, writer on 3 (same CCX as risk on 4) | 0.25 µs | 0.40 µs |

The writer prints about 11 lines per trade report (226,885 lines for 20,000
reports). The "no prints" rows remove the `std::cout` calls from
`ClientStatesWriter::Handle` (both overloads) and
`ClientStates::SetClientAssets` in `order_gateway/include/`. **Not yet
applied to the repository.**

Sharing the core costs three orders of magnitude; the prints cost about 2×
on top of a clean core. Fix the pinning first.

### Torn reads

**Method.** The writer moves 1 unit from asset 15 to asset 1 on every
update, so every published state has a fixed sum, with the two fields on
different cache lines. A read whose sum is wrong is torn.

Real `GetClientState` copies: 0 torn reads in 80M, with the writer flat out
or at 1 update per µs. With the reader paused between the two fields,
standing in for an interrupt in the middle of its copy:

| Pause | Writer flat out | Writer 1 per µs |
|---|---|---|
| 50 ns | 0.013% torn | 0.015% |
| 1 µs | 99.9% | 1.1% |
| 5 µs | ~100% | ~100% |

After two flushes the writer folds deltas into the buffer it published two
flushes earlier, so a read that spans two flushes of the same client mixes
generations. Nothing detects it; a seqlock would retry.

---

## External references

Sanity checks against published numbers.

| What | Here | Reference |
|---|---|---|
| Core-to-core, CAS method (port of nviennot `cas.rs`) | SMT 9.4 ns, same CCX 19-24 ns, across CCXs 62-88 ns | nviennot/core-to-core-latency: Zen 3 SMT 7.8 ns, same group 16-17 ns, across 84-85 ns; Zen+ 24 / 92 ns |
| Core-to-core, load-then-store (§ 2) | 40 / 53 / 90 ns | About 2× the CAS method: each hop is a read miss then an ownership upgrade, like an SPSC queue |
| SPSC queue round trip, same CCX | 180 ns (2 × 90) | rigtorp SPSCQueue on Ryzen 9 3900X (Zen 2), same chiplet: 133 ns |
| Inter-thread hop | 90 ns one way, same CCX | LMAX Disruptor paper (i7-2720QM, Java): 52 ns mean; `ArrayBlockingQueue` 32,757 ns mean, P99 2.1 ms |
| Scheduler slice | 1.5 ms median / 6.0 ms P99 wait with 3 spinners | `kernel/sched/fair.c`: base slice 0.70 ms × (1 + ilog2(min(ncpus, 8))) = 2.8 ms here |
| Timer tick | `std::map` P99.9 artefact | `CONFIG_HZ=1000`: one tick per ms |
| Hash map find | Level with absl | Ankerl 2022 (i7-8700): `absl::flat_hash_map` among the fastest for integer find |
| Exchange | 600 ns matching P99, in-process | CME Globex 2012: 52 µs median router → match engine, network and gateway included |
