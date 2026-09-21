# Naoto Exchange

A low-latency exchange system in C++20: order gateway, account service,
matching engine, and market data distribution over DPDK. Runs either as a
distributed cluster with etcd-based service discovery and runtime elasticity,
or as three processes sharing memory on the hot path.

P99 of 600 ns in matching and 1.1 µs to gateway ack at 40k msg/s on an AMD
Ryzen 5 4600H (6 cores / 12 threads, single NUMA node), Linux 7.0.0, over a
veth pair with `net_af_packet`. Cores 0-9 isolated with `nohz_full` and
`rcu_nocbs`, IRQs pinned to 10-11, max C-state 1.

Everything runs on `net_af_packet` virtual devices over a veth pair, so no
special NIC is needed to try it. A machine with more than 6 physical cores is
recommended.

---

## What's here

| Component | Role |
|---|---|
| `order_gateway` | TCP ingest, risk checks, in-flight exposure, order acks |
| `matching_engine` | Price-time priority matching, order book, fill generation |
| `account_service` | Authoritative client state, kept current in real time in place of a database. Also handles auth requests and API key checks. |
| `trading_dashboard` | UI: manual trading interface and live latency statistics |

---

## Building

### 1. Dependencies

```bash
git clone https://github.com/MaximeCorp/naoto-exchange
cd naoto-exchange
./bootstrap/run-all.sh
```

This pulls and compiles DPDK and etcd-cpp-apiv3 into `artifacts/`. **Expect 5
to 10 minutes for DPDK alone.** Abseil and pkg-config are found on the system;
install them first if configure fails.

### 2. Configure and build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

> `NAOTO_SANITIZE` is off by default, which is what you want for timing: ASan
> redzones change object layout, so cache-line isolation does not hold and any
> measurement taken under it is meaningless. Turn it on for correctness runs
> only.

### Build options

| Option | Default | Effect |
|---|---|---|
| `NAOTO_SANITIZE` | `OFF` | AddressSanitizer. Never benchmark with this. |
| `NAOTO_NATIVE_ARCH` | `ON` | `-march=native`. Off falls back to `-msse4.2`. |
| `NAOTO_PERF` | `OFF` | RDTSC timestamping and latency reporting. |
| `NAOTO_SHARED_MEMORY` | `OFF` | Gateway/matching over shared-memory SPSC instead of TCP. Three processes, no elasticity. |
| `NAOTO_BUILD_TESTS` | `OFF` | GoogleTest suite under `tests/`. |
| `NAOTO_LTO` | `OFF` | Whole-program LTO. Slow, RAM-heavy link. |

Builds with `-Wall -Wextra -Werror`. `ccache` is used automatically if present.

---

## Running

### 1. Set up the local environment

```bash
./market_server/scripts/setup_dpdk_test_env.sh
```

Creates the veth pair and hugepages the vdev arguments below expect.

### 2. Launch the services

Each takes DPDK EAL arguments before the `--` separator. Pick cores that are
not shared between services. The distinct `--file-prefix` is what lets the
gateway and account service both sit on `veth1` without colliding over DPDK's
shared hugepage state.

```bash
sudo ./build/market_server/order_gateway/order_gateway \
    -l <core> --file-prefix=gw --huge-dir=/dev/hugepages/ \
    --vdev=net_af_packet0,iface=veth1,qpairs=1 --

sudo ./build/market_server/account_service/account_service \
    -l <core> --file-prefix=as --huge-dir=/dev/hugepages/ \
    --vdev=net_af_packet0,iface=veth1,qpairs=1 --

sudo ./build/market_server/matching_engine/matching_engine \
    -l <core>,<core> --file-prefix=me --huge-dir=/dev/hugepages/ \
    --vdev=net_af_packet0,iface=veth0,qpairs=2 --
```

The two lcores given to the matching engine are for its DPDK threads. Its
other threads, including matching itself, pin separately and are not covered
by the EAL `-l` argument. That is part of why more than 6 physical cores is
the recommendation.

**Startup order.** etcd must be running before any service starts. With
`NAOTO_SHARED_MEMORY=ON`, launch the matching engine before the order gateway,
since the gateway attaches to the shared-memory segment the engine creates.

### 3. Dashboard

```bash
MCAST_IFACE=veth1 ./build/market_client/trading_dashboard
```

All demo accounts share one hard-coded key. This is a local test credential
with nothing behind it, not a leaked secret:

```
aa92e9c3316ddc46dc882b45fe9f07b58230e83f3e3290ffbb5e2d6fa80ebd4f
```

### 4. Stress test

`realistic_stress_test.cpp` drives the system under a realistic order mix and
is the quickest way to see it working under load.

### 5. Core layout

Core assignment is machine-specific and there is no set of numbers that is
right everywhere, so work out your own from `lscpu -e`. What matters:

- Keep hot threads off hyperthread siblings of each other.
- Keep threads that hand work to each other on the same NUMA node, and on the
  same L3 where the topology splits it.
- Leave the cores handling interrupts and kernel housekeeping outside the
  isolated set.
- The DPDK threads that are not on the hot path can share non-isolated cores
  if cores are scarce. The matching engine's emitter threads are the exception:
  put them on a shared core and they become the bottleneck. The symptom is
  misleading, since it shows up as gateway-to-matching latency rather than on
  the emit side. Slow emitters stop draining their queues, the queues fill, and
  the backpressure lands upstream.

Isolation is done with `isolcpus` on the kernel command line, and is worth
doing before taking any measurement: without it, scheduler interference shows
up directly in the tail percentiles. A fixed frequency governor helps for the
same reason.

### 6. Tuning

`market_server/common/include/system_conf.hpp` holds the tunable parameters:
queue depths, batch sizes, pool sizes. The cores each thread pins to are not
in there yet and are set in the source of each service, so changing them means
editing and rebuilding.

---

## Latency measurement

Build with `-DNAOTO_PERF=ON`. Four timestamps are taken per message:

1. Gateway internal (risk check + ack)
2. Shared-memory SPSC handoff to matching
3. Matching
4. Tick to trade

Each is reported at min / P50 / mean / P95 / P99 / max, and shown live in the
dashboard alongside the order book.

---

## Tests

**Currently not building.** The suite under `tests/` predates a deep refactor
and has not been brought back in line with it yet. `NAOTO_BUILD_TESTS` is off
by default, so this does not affect a normal build. Fixing the suite is on the
list below.

Once it builds:

```bash
cmake -B build -DNAOTO_BUILD_TESTS=ON -DNAOTO_SANITIZE=ON
cmake --build build -j
ctest --test-dir build
```

Around 200 GoogleTest cases, intended to be run under AddressSanitizer.

---

## Status

Personal R&D project, active since July 2025. Working end to end.

Failure-path handling under resource exhaustion is in progress and marked with
TODOs in the source. The intended policy: fill reports are never dropped, order
book updates are recoverable through sequence-gap resync, and pool exhaustion
means a sizing assumption was wrong rather than a condition to degrade through.

Known inconsistencies remain in client state updates on edge cases. Market
order fill updates are the clearest one: they should only update the client
states held by services that froze funds before the order reached the matching
engine, and there is currently no mechanism to tell those services apart from
the rest. Adding that is next.

Also planned: bringing the test suite back in line with the refactored code,
and optional PostgreSQL backing so the account service can load client state
from a durable store at startup rather than beginning empty.

## Contact

Maxime Himeno, maxime.himeno@gmail.com
