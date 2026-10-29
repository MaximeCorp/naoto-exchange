# market_server unit tests

GoogleTest suite for the server-side components. It needs neither DPDK
hardware nor a running etcd. The `EpollServer` tests use real loopback TCP
sockets rather than mocks, since the socket handling is the thing under
test.

## Status

**Does not currently build.** The suite was written against the code before
the September 2026 refactor (moodycamel queues replaced by the in-house
`SpscQueue`, types moved into `*_types.hpp`, wire types renamed) and has not
been updated since. Last known result before the refactor: **181 tests, 181
passing**, in Debug, Release (`-O3`), ASan + UBSan, and repeated
`--gtest_shuffle` runs.

The rest of this file describes the suite as it was then, corrected where
the source has since changed.

## Build and run

```bash
cmake -B build-tests -DNAOTO_BUILD_TESTS=ON -DNAOTO_SANITIZE=ON
cmake --build build-tests -j --target market_server_unit_tests
./build-tests/tests/market_server/unit/market_server_unit_tests
# or: ctest --test-dir build-tests
```

| Option | Default | Effect |
|---|---|---|
| `NAOTO_BUILD_TESTS` | `OFF` | Builds `tests/` at all |
| `NAOTO_SANITIZE` | `OFF` | ASan for the whole tree. Turn it on for test runs. |
| `NAOTO_TESTS_UBSAN` | `ON` | Adds UBSan to the test binary only. Several of the bugs below were found by it. |

Use a **separate, fresh build directory** for sanitizer builds. CMake caches
compiler flags on the first configure; passing different `CXXFLAGS` to a later
configure of the same directory is silently ignored. An earlier "clean under
ASan" result was wrong for exactly this reason. `NAOTO_SANITIZE` avoids the
problem because it is a CMake option, not an environment variable.

### Directory layout

```
tests/
  CMakeLists.txt                 declares GoogleTest once, adds both suites
  market_server/
    CMakeLists.txt
    unit/
      CMakeLists.txt             three OBJECT libraries, see below
      test_*.cpp
  market_client/                 placeholder, no tests yet
```

`tests/` is added from the root `CMakeLists.txt` after the
`market_server/*` subdirectories, because the suite links the real `common`
target.

### Why the test binary is built from three object libraries

`order_gateway/include` and `account_service/include` both contain
`client_state.hpp`, `client_states.hpp`, `client_delta.hpp` and
`client_states_writer.hpp`: two independent triple-buffer implementations,
same file names, different namespaces. Each includes its siblings by bare
name. The services never put both directories on one include path, but a
single test binary needs both, and no include order resolves
`#include <client_state.hpp>` correctly for both in one translation unit.

`unit/CMakeLists.txt` therefore splits the test sources into three `OBJECT`
libraries with different include priority: gateway first, account service
first, and everything else.

## Coverage

| Area | Covered |
|---|---|
| `common` | `SkipList`, `FlatHashMap`, `StoragePool`, `SingleThreadedStoragePool`, `SequenceRingBuffer`, `BytesBuffer`, `ObjectBatch` / `ObjectBuffer`, `VersionedFd`, wire struct layouts, `EpollServer` (accept, full / partial / multi-message reads, orderly close, `AcceptHandle`, `CloseHandle`, `BatchHandle`) |
| `matching_engine` | `PriceLevel`, `OrderBook`, `BidAsk`: crossing, price-time priority, multi-level sweeps, cancels, partial fills |
| `order_gateway` | `ClientState`, `LocalAttempts`, `ClientStates` (triple buffer), `ClientStatesWriter` (stale-response path, `UpdatesBuffer` replay), `CheckOrderRisk` (`order_risk_check.hpp`) |
| `account_service` | `ClientState`, `ClientStates`, `ClientStatesWriter`, `ClientRequestProcessor` (connect, auth, disconnect) |

### Not covered

- **`OrderRouter` as a class.** Its constructor makes blocking etcd calls
  against `ETCD_ADDR`, so it cannot be built without a live etcd. Its risk
  logic was extracted into `order_risk_check.hpp` (a free function, same
  behaviour) so that part is tested; order forwarding, the resend buffers and
  the etcd watch callbacks are not.
- Anything that needs DPDK or etcd: the multicast emitters and receivers,
  service discovery.
- `EpollServer`'s `FirstMessageHandle` path (needs a fixture instantiated
  with a non-`monostate` `InitMessage`).
- `main.cpp` of each service.
- Anything multi-threaded end to end. No test runs a real
  gateway → engine → gateway round trip across threads.

## Open findings

Tests named `*_LIKELY_BUG` assert what the code **does**, not what it should
do. They pin behaviour that needs a design decision before it is changed.

| Finding | Test | State in current source |
|---|---|---|
| Marketable limit orders cancelled their unfilled remainder instead of resting it | `BidAskTest.MarketableLimitBuyPartialFillCancelsRemainder_LIKELY_BUG` | **Changed.** `FillBuyOrder` / `FillSellOrder` now rest a `LIMIT` remainder via `AddBuyLimitOrder` / `AddSellLimitOrder`. This test will fail once the suite builds and should be flipped to assert resting. |
| A `MARKET` buy with `Price = 0` never matches: `FillBuyOrder` stops when `BestAskPrice > order.Price` without checking the order type | `BidAskTest.MarketBuyOrderWithZeroPriceNeverMatches_LIKELY_BUG` | Still true. |
| `ClientAccountSnapshot` is packed with `char Status` first, so every following multi-byte field is misaligned. UBSan flags it; values are correct on x86 / GCC. Fixing it changes the wire format. | (UBSan) | Still true. |
| Gateway ID 10 bypasses the authorised-gateway check in `ClientRequestProcessor`. Intentional for the dashboard, marked `TODO`. | `AllowsGatewayIdTenRegardlessOfAuthorization` | Still true. |

## Bugs the suite found

Fixed in the source at the time, confirmed by building and running under
Debug, Release, ASan and UBSan. The `BUG FIX:` comments that marked each one
were removed during the refactor. Spot checks against the current source are
noted.

| # | Component | Bug | Now |
|---|---|---|---|
| 1 | `FlatHashMap::GetVal` | Never searched past the first 16-slot probe window, so keys with a longer probe distance were not found | Fixed. A second, separate bug in `AddNode` was found later by the benchmarks ([docs/BENCHMARKS.md](../../docs/BENCHMARKS.md) § 1) and is **not** fixed. |
| 2 | `BidAsk::CancelOrder` | Cancel of an unknown order ID dereferenced null and crashed the engine. Reachable from any client. | Fixed (checked) |
| 3 | gateway `ClientState` | Constructor left fields uninitialised; fresh clients started with garbage balances at `-O3` | Fixed |
| 4 | account `ClientState` | Same, plus `SetConfirmed` / `SetAttempt` assigned a scalar to a `std::array` | Fixed |
| 5 | account `ClientStates::FlushTripleBuffer` | Did not carry `AssetId`, `ClientId`, `Key`, `Authorized`, `Connected` into the next buffer; after one flush asset lookups failed, later `CheckKey` failed | Fixed |
| 6 | account `ClientStates::SetClientState` | Overwrote one buffer directly instead of recording a delta | Fixed |
| 7 | account `ClientStates` | `SequenceIds[slot][client]` instead of `[client][slot]` at four sites; heap overflow with fewer than 3 clients | Fixed (checked) |
| 8 | gateway `client_states_writer.hpp` | Used `ObjectBatch` without including its header | Fixed |
| 9 | `skip_list.hpp` | Used `__rdtsc()` without `<x86intrin.h>` | Fixed |
| 10 | `StoragePool::getAvailable` | `const` method calling a non-`const` queue method; dead code that could not compile | Fixed |
| 11 | `BytesBuffer::Add` | Never advanced `Size`; a second `Add` overwrote the first | Fixed (checked) |
| 12 | `PriceLevel::ClearPriceLevel` | Leaked the last node of the list back to the pool | Fixed (checked) |
| 13 | account `ClientState` | 2- and 3-argument constructors ignored `confirmed` / `attempt` | Fixed |
| 14 | `EpollServer::readMessage` | Enqueued an empty batch when only part of a message had arrived | Fixed |
| 15 | gateway `ClientStatesWriter` | One stale response aborted the rest of its batch (`return` instead of `continue`) | Fixed (checked) |
| 16 | `ClientRequestProcessor` | `'D'` (disconnect) changed a local copy of the client state and never stored it, so a client could never reconnect | **Not in current source.** The `'D'` branch is now empty. Reconnection still works only because the "already connected" check is commented out. |

### Mistakes in earlier versions of this suite

Kept because both are easy to repeat:

- A test once claimed `PriceLevel::PopOrder()` does not decrement
  `TotalAmount`. It does. The test was wrong, not the code; it is now
  `PriceLevelTest.PopOrderDecrementsTotalAmount`.
- The `BytesBuffer` test asserted the value of bytes `Add()` never writes.
  They are uninitialised. The prediction matched in a standalone repro but
  not inside the full test binary, where earlier tests had used the same
  stack. Verify against the real test binary, not an isolated repro.

## Toolchain notes

- `test_object_batch_buffer.cpp` passes an oversized length to
  `ObjectBuffer::addBytes()` on purpose. Some GCC versions prove it too large
  after inlining and fail with `-Werror=array-bounds`. The `OpaqueSize()`
  helper (an `asm volatile` register barrier) hides the constant without
  changing runtime behaviour.
- `FlatHashMap`'s constructor zero-initialises `FootPrints`, `Keys` and
  `Data`, not just `Tags`. That silences a `-Wmaybe-uninitialized` false
  positive at `-O3`.

## Next steps

1. Bring the suite back in line with the refactored code.
2. Flip `MarketableLimitBuyPartialFillCancelsRemainder_LIKELY_BUG` to assert
   resting, and decide on the zero-price market buy.
3. Restore the `'D'` disconnect handling (#16) together with the "already
   connected" check.
4. Inject the etcd and socket layer into `OrderRouter` so it can be tested
   like `order_risk_check.hpp`.
5. Add a fixture for `EpollServer`'s `FirstMessageHandle` path.
