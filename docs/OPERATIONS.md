# Operations

How to configure, start, check and troubleshoot a Naoto Exchange deployment.
Build instructions are in the [root README](../README.md#building). What each
component does is in [ARCHITECTURE.md](ARCHITECTURE.md).

All commands are run from the repository root.

---

## 1. Local test topology

The default setup runs everything on one host over a veth pair, using DPDK's
`net_af_packet` virtual device so no DPDK-capable NIC is needed.

```
                       ┌────────────────── one host ──────────────────┐
                       │                                              │
   matching_engine ────┤ DPDK af_packet TX                            │
   (EAL --file-prefix=me)   │                                         │
                       │  veth0  10.10.10.1 ═══════╗                  │
                       │                           ║ veth pair        │
                       │  veth1  10.10.10.2 ═══════╝                  │
                       │       │        │        │                    │
   order_gateway ──────┤ DPDK af_packet RX (--file-prefix=gw)         │
   account_service ────┤ DPDK af_packet RX (--file-prefix=as)         │
   trading_dashboard ──┤ kernel UDP socket, IP_ADD_MEMBERSHIP on veth1│
                       │                                              │
   All TCP (client ↔ gateway ↔ account service ↔ engine) uses         │
   loopback, not the veth pair.                                       │
                       └──────────────────────────────────────────────┘
```

`market_server/scripts/setup_dpdk_test_env.sh` builds this: it reserves
1024 × 2 MB hugepages, creates the veth pair, disables reverse-path
filtering, and sends a test multicast datagram across the pair to prove the
network works before you start anything. It tears down any previous state
first, so it is safe to re-run. Re-run it after every reboot.

---

## 2. Addresses and ports

All of these are hard-coded in each service's `src/main.cpp` or in
`etcd` setup code. Changing one means editing the source and rebuilding
every binary that refers to it.

| What | Value | Defined in |
|---|---|---|
| Gateway client port | TCP 8081 | `order_gateway/src/main.cpp` |
| Matching engine order port | TCP 8080 | `matching_engine/src/main.cpp` |
| Account service port | TCP 8082 | `account_service/src/main.cpp` |
| Order state reports | UDP 239.1.1.1:30001 | `matching_engine/src/main.cpp` |
| Order book updates | UDP 239.1.1.2:30002 | `matching_engine/src/main.cpp` |
| Multicast source | 10.0.0.20:30000 | `matching_engine/src/main.cpp` |
| etcd | `localhost:2379` | `ETCD_ADDR` (below) |
| Shared-memory queue | `/dev/shm/me_gw_shm` | `system_conf.hpp` (`SharedMemoryPath`) |

---

## 3. Environment variables

| Variable | Service | Default | Effect |
|---|---|---|---|
| `ETCD_ADDR` | all three | `localhost:2379` (gateway: `http://localhost:2379`) | etcd endpoint |
| `LISTEN_ADDR` | all three | engine `127.0.0.1:8080`, gateway `localhost:8081`, account `127.0.0.1:8082` | Address **advertised** in etcd. Does not change the port actually bound. |
| `SYMBOL` | matching engine | `1` | etcd key suffix and advertised `asset_id`. Does **not** change the asset the book trades, which is hard-coded to 1 in `main.cpp`. Change both together. |
| `REPORT_SEQUENCE_ID`, `ORDER_BOOK_SEQUENCE_ID` | matching engine | `0` | Seed the matching thread's counters. Currently has no visible effect: the emitters overwrite sequence numbers (see [PROTOCOL.md § 5.4](PROTOCOL.md#54-sequence-numbers)). |
| `MACHINE_ID` | gateway | `0` | Gateway ID used in the handshake to the account service, the etcd key, and order IDs. |
| `GATEWAY_ID` | gateway | `0` | Gateway ID put into auth requests; the account service routes the reply by it. **Must equal `MACHINE_ID`.** |
| `ORDERS_COUNT` | gateway | `0` | Starting value of the order ID counter. Set it when restarting a gateway against a running engine to avoid reusing IDs. |
| `MCAST_IFACE` | dashboard | empty (kernel picks) | Interface to join the multicast groups on. `veth1` in the local setup. |

---

## 4. Startup

### 4.1 Prerequisites

1. Built binaries (see [README](../README.md#building)).
2. `sudo ./market_server/scripts/setup_dpdk_test_env.sh` has passed its
   verification step.
3. An etcd v3 server reachable at `ETCD_ADDR`. The bootstrap scripts build
   only the etcd **client** library, not the server. On Ubuntu 24.04, for
   example:

   ```bash
   sudo apt install etcd-server
   etcd --data-dir "$(mktemp -d)"     # listens on localhost:2379
   ```

### 4.2 Order

```
  etcd ─▶ matching_engine ─▶ account_service ─▶ order_gateway ─▶ clients / dashboard
          (must precede the     (gateway also finds it later
           gateway in shm mode)  through the etcd watch)
```

- **etcd first.** Every service registers on startup and does not retry.
- **Shared-memory mode: engine before gateway.** The engine creates
  `/me_gw_shm`, wiping any previous one. The gateway polls for it every
  100 ms. A gateway left running across an engine restart keeps the old
  mapping and stops delivering orders; restart both.
- **TCP mode:** order is flexible. The gateway watches etcd and connects to
  engines and the account service as they appear.

Launch commands with DPDK EAL arguments are in the
[README](../README.md#2-launch-the-services).

### 4.3 Checking it is up

```bash
# Every service should have a key here
etcdctl get --prefix / --keys-only
#   /account-service/0
#   /matching-engines/1
#   /socket-gateways/0

# Thread placement matches what you expect
ps -L -o pid,tid,psr,comm -p "$(pgrep -d, -f 'order_gateway|matching_engine|account_service')"

# Multicast group joined on the receive side (while the dashboard runs)
ip maddr show dev veth1 | grep 239.1.1
```

Then run the stress test (§ 6) and watch the dashboard's book fill in.

### 4.4 Restarting a component

There is no persistence anywhere. Restarting a component loses:

| Component | Lost on restart |
|---|---|
| matching engine | The whole book. Sequence numbers restart at 0, so a long-running consumer sees them go backwards. |
| account service | Every balance change since startup. It reloads the 12 seeded demo clients. |
| gateway | All sessions; clients must reconnect and log in. The order ID counter restarts at `ORDERS_COUNT`. |

---

## 5. Core layout

Thread pinning is hard-coded in each service (`setAffinity` calls in
`order_gateway_service.hpp`, `matching_engine.hpp`, `account_service.cpp`).
The `Account*Core` constants in `system_conf.hpp` are not read by anything.

Reference machine, Ryzen 5 4600H: two CCXs with separate L3, SMT pairs are
(0,1), (2,3), …

```
          CCX 0  (L3 4 MB)                         CCX 1  (L3 4 MB)
  ┌─────────┬─────────┬─────────┐        ┌─────────┬─────────┬─────────────────────┐
  │ cpu 0 1 │ cpu 2 3 │ cpu 4 5 │        │ cpu 6 7 │ cpu 8 9 │ cpu 10        11    │
  └─────────┴────┬────┴────┬────┘        └────┬────┴────┬────┴──┬─────────────┬────┘
                 │         │                  │         │       │             │
            GW epoll   GW risk           ME epoll   ME match   GW states    GW DPDK RX
             (2)        (4)               (6)        (8)       writer       GW req sender
                                         TCP only              GW acct rx   AS processor
                                                               GW ack send  AS dispatcher
                                                               AS epoll
                                                               AS writer
  isolated: 0-9 (nohz_full, rcu_nocbs)                        IRQs: 10-11
```

The matching engine's two DPDK emitter lcores come from its EAL `-l`
argument and are not shown; give them their own isolated cores.

Things this layout shows that are worth knowing before changing it:

- **Risk (4) to matching (8) crosses CCXs.** In shared-memory mode that is
  the order hop. A one-way cache-line transfer measured 53 ns within a CCX
  and 90 ns across on this machine
  ([BENCHMARKS.md](BENCHMARKS.md) § 2).
- **Cores 10 and 11 each run several busy-polling threads**, from two
  different services, on the same cores that take IRQs. That is fine for
  getting it running and bad for tail latency on acks and client-state
  freshness. Same file, § 2 and § 3.
- General rules for choosing your own layout are in the
  [README](../README.md#5-core-layout).

---

## 6. Load generation

`market_server/functional_tests/realistic_stress_test.cpp` runs maker, noise
and taker bots against a gateway. It has no CMake target. Build it with the
same `NAOTO_PERF` setting as the services, or the `Order` size will not
match:

```bash
g++ -std=c++20 -O3 -march=native -pthread \
    -Imarket_server/common/include -Imarket_server/order_gateway/include \
    market_server/functional_tests/realistic_stress_test.cpp \
    -o build/realistic_stress_test          # add -DNAOTO_PERF if the services have it

./build/realistic_stress_test --rate 40000 --duration 30
./build/realistic_stress_test --help        # all options
```

The Python scripts in the same folder predate the current wire format and do
not work against it.

---

## 7. Troubleshooting

| Symptom | Likely cause | Check / fix |
|---|---|---|
| Dashboard shows no book updates or trades | Multicast not reaching the receiver | Re-run `setup_dpdk_test_env.sh`; its verification step isolates the network. Then check `MCAST_IFACE=veth1`. |
| Dashboard receives packets but values are nonsense | `NAOTO_PERF` differs between the engine and the dashboard build | Rebuild everything from one build tree. Struct sizes are in [PROTOCOL.md § 1](PROTOCOL.md#build-dependent-layouts). |
| Dashboard "Refresh Balance" / "Query" do nothing | Dashboard looks for `/client-details-provider/`, account service registers `/account-service/0` | `etcdctl put /client-details-provider/0 '{"addr":"127.0.0.1:8082","status":"active"}'` |
| Dashboard cannot connect to the gateway | Fallback port is 8000, gateway listens on 8081 | Pick the discovered gateway in the panel, or type 8081. |
| Every order rejected `UserNotConnected` | Login snapshot never applied | Is the account service key in etcd? Did the gateway log `new account service found`? Is `GATEWAY_ID == MACHINE_ID`? Did the client send the raw key, not its hash? |
| Login worked once, later logins never complete | Snapshot discarded as stale after 8192 trade reports | Known issue, [ARCHITECTURE.md Known gaps](ARCHITECTURE.md#known-gaps) item 2. Restart the gateway. |
| Every order rejected `TechnicalFailure` | Gateway has no connection to an engine for that `AssetId` (TCP mode) | `etcdctl get /matching-engines/<asset>`. Note `SYMBOL` must match the book's asset. |
| Orders accepted, nothing ever fills, no reports | Shared-memory mode, gateway attached to an old `/me_gw_shm` | Restart the gateway after the engine. |
| Second gateway or account service fails to start | DPDK hugepage state collision | Each DPDK process needs its own `--file-prefix`. |
| `rte_eal_init failed` | No hugepages, or not root | Re-run the setup script; run services with `sudo`. |
| New client connections refused at the gateway | `RLIMIT_NOFILE` capped at 128 | The gateway sets its fd limit to `GatewayMaxClients` (128) because client state is indexed by fd. DPDK, gRPC and etcd sockets count against it. Raise `GatewayMaxClients`. |
| Latency tail jumps by milliseconds | Busy-polling threads sharing a core, or cores not isolated | `ps -L -o psr`; see § 5 and [LATENCY.md](LATENCY.md#4-reading-the-numbers-critically). |
| Gateway prints thousands of lines per second | Per-report logging in `ClientStatesWriter` | Expected today. Redirect stdout to a file or `/dev/null`; printing to a terminal is slow enough to back up the queues. |
