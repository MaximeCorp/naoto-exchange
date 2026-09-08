#pragma once
//
// app_state.hpp
//
// Everything in here is owned and mutated EXCLUSIVELY by the render/main
// thread. Network/watcher threads never touch this directly -- they push
// into the MpscQueues declared alongside it in main.cpp, and the render
// loop drains those queues once per frame and applies the results here.
//
// This is the "hand data off to the main thread via an atomic/queue"
// rule from the project's general threading convention, applied
// uniformly across etcd, both multicast feeds, and every TCP session.
//

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "etcd_watcher.hpp"
#include "wire_formats.hpp"

// ---- Order book: per asset, per side, price -> depth ---------------------
// Depth == 0 removes the level (see OrderBookUpdate semantics). Asset 0
// (the settlement/dollar leg) is never shown here -- it's not a
// tradeable order-book asset.
struct OrderBook
{
    std::map<int64_t, uint32_t, std::greater<int64_t>> bids; // best bid first
    std::map<int64_t, uint32_t> asks; // best ask first

    // Returns the signed depth delta (new - old, old=0 if the level
    // didn't previously exist) -- used upstream to derive an
    // approximate trading-volume proxy from raw depth changes, since
    // there's no dedicated historical-data feed yet.
    int64_t apply(const naoto::OrderBookUpdate &u)
    {
        // bids/asks have different map types (distinct comparators), so
        // this can't be unified via a single reference/ternary -- same
        // logic duplicated per side instead.
        if (u.Side == ORDER_BOOK_UPDATE_BUY)
        {
            auto it = bids.find(u.Price);
            int64_t old_depth =
                it != bids.end() ? static_cast<int64_t>(it->second) : 0;
            if (u.Depth == 0)
                bids.erase(u.Price);
            else
                bids[u.Price] = u.Depth;
            return static_cast<int64_t>(u.Depth) - old_depth;
        }
        else
        {
            auto it = asks.find(u.Price);
            int64_t old_depth =
                it != asks.end() ? static_cast<int64_t>(it->second) : 0;
            if (u.Depth == 0)
                asks.erase(u.Price);
            else
                asks[u.Price] = u.Depth;
            return static_cast<int64_t>(u.Depth) - old_depth;
        }
    }
};

// ---- Approximate price/volume history, derived ONLY from order book
// updates (no dedicated historical-data service yet -- this is a
// deliberately rough stand-in per your own framing, not authoritative).
//
// PRICE: every OrderBookUpdate carries a Price, so each update is
// treated as a price observation at its Timestamp.
//
// VOLUME: approximated from depth DECREASES only -- when a price
// level's resting depth shrinks, that's treated as a proxy for trading
// activity at that price. This is NOT the same as real trade volume:
// a depth decrease could equally be a cancellation, not a fill, and
// this has no way to tell those apart from the order book feed alone.
// Depth INCREASES (new resting supply appearing) are never counted as
// volume. Once a real historical-data service exists with actual trade
// records, this whole approximation should be replaced, not extended.
struct PriceVolumeCandle
{
    uint64_t bucket_start_ms = 0;
    double open = 0, high = 0, low = 0, close = 0;
    double volume = 0;
};

struct AssetPriceHistory
{
    std::deque<PriceVolumeCandle> candles; // oldest first
    uint64_t active_bucket_ms = 0;
    static constexpr size_t kMaxCandles = 500; // capped, avoid unbounded growth
                                               // over a long session

    void record(uint64_t timestamp_ms, int64_t price,
                double volume_contribution, uint64_t bucket_size_ms)
    {
        if (bucket_size_ms == 0)
            bucket_size_ms = 1000;
        if (bucket_size_ms != active_bucket_ms)
        {
            // Timeframe changed (e.g. UI timeframe picker) -- start fresh
            // rather than mixing differently-sized candles, which would
            // otherwise render as visually inconsistent bar widths.
            candles.clear();
            active_bucket_ms = bucket_size_ms;
        }
        uint64_t bucket_start =
            (timestamp_ms / bucket_size_ms) * bucket_size_ms;
        double p = static_cast<double>(price);

        if (candles.empty() || candles.back().bucket_start_ms != bucket_start)
        {
            // Out-of-order/late data (bucket older than what we've already
            // recorded) is simply dropped rather than rewriting history --
            // keeps this append-only and simple, acceptable for a rough
            // stand-in view.
            if (!candles.empty()
                && bucket_start < candles.back().bucket_start_ms)
                return;
            PriceVolumeCandle c;
            c.bucket_start_ms = bucket_start;
            c.open = c.high = c.low = c.close = p;
            c.volume = 0;
            candles.push_back(c);
            while (candles.size() > kMaxCandles)
                candles.pop_front();
        }

        PriceVolumeCandle &c = candles.back();
        c.high = std::max(c.high, p);
        c.low = std::min(c.low, p);
        c.close = p;
        c.volume += volume_contribution;
    }
};

// ---- Trade tape / "chat" entry -------------------------------------------
struct TradeFeedEntry
{
    uint32_t sequence_id;
    uint32_t trade_id;
    uint32_t client_id;
    uint32_t order_id;
    uint16_t bought_asset;
    uint16_t sold_asset;
    int64_t bought_delta;
    int64_t sold_delta;
    int64_t sold_attempt_delta;
    naoto::OrderState state; // FILL/PARTIAL_FILL/CANCEL/REJECT/ADD --
                             // changes how this entry should read,
                             // see draw_trade_feed_panel in main.cpp
    // Raw timestamps, straight off the wire -- units are whatever rtcd
    // produces (NOT confirmed to be nanoseconds; see kNsPerRawTimeUnit
    // in main.cpp), kept raw here rather than pre-converted so the
    // conversion factor can be adjusted live without needing to re-derive
    // history. Deltas between these are meaningful even if the absolute
    // values/units aren't -- see naoto::OrderStateReport's own comment.
    uint64_t ingested_ts;
    uint64_t routed_ts;
    uint64_t received_ts;
    uint64_t update_ts;
};

// ---- Live-updated balance for one connected trading session --------------
// Seeded by the last ClientDetailsPoller snapshot, then nudged instantly
// by matching OrderStateReport deltas as they stream in (optimistic
// update), reconciled back to ground truth on the next poll.
struct LiveBalance
{
    // asset_id -> (confirmed, attempt)
    std::map<uint16_t, std::pair<int64_t, int64_t>> positions;
    bool has_snapshot = false;
    std::string last_error;

    void apply_confirmed_delta(uint16_t asset_id, int64_t delta)
    {
        positions[asset_id].first += delta;
    }

    void apply_attempt_delta(uint16_t asset_id, int64_t delta)
    {
        positions[asset_id].second += delta;
    }
};

struct AppState
{
    // asset_id -> order book (asset 0 intentionally never inserted here)
    std::map<uint16_t, OrderBook> order_books;

    // asset_id -> rough price/volume history derived from order book
    // updates -- see AssetPriceHistory's own comment for the important
    // caveats on what "volume" means here.
    std::map<uint16_t, AssetPriceHistory> price_history;
    uint64_t price_history_bucket_ms = 1000; // 1s candles by default;
                                             // exposed so the UI can
                                             // offer a timeframe picker

    // Most recent N trades, newest last. Capped well above what a
    // typical dashboard session needs, purely to bound memory over a
    // very long-running session -- NOT meant to be a tight limit day to
    // day (at ~80 bytes/entry, 200k entries is ~16MB). If you genuinely
    // want no cap at all, this is the one line to remove/guard, but be
    // aware this dashboard is meant to run for extended live-trading
    // sessions (per its own doc comments), so truly unbounded growth
    // here is a real long-session memory-exhaustion risk, not a
    // theoretical one -- ask before going fully unbounded in production.
    //
    // std::deque, not std::vector: push_trade() below evicts from the
    // FRONT once over the cap, and front-erase on a vector is O(n) --
    // it has to shift every remaining element down. That's fine at the
    // old 2000 cap, but at 200k it would mean shifting up to ~16MB on
    // every single push once full, every frame that streams a new
    // trade. deque's front-erase is O(1) amortized instead -- same
    // interface throughout this file (push_back, erase(begin,it),
    // range-for), no other code needed to change.
    std::deque<TradeFeedEntry> trade_feed;
    static constexpr size_t kMaxTradeFeed = 200000;

    // Raw etcd entries under /matching-engines/, key -> value. See the
    // open schema question in etcd_watcher.hpp -- value is opaque text
    // for now.
    std::map<std::string, std::string> etcd_entries;

    // ---- Structured service discovery from etcd (see etcd_watcher.hpp) --
    // matching-engine key -> parsed info (addr, asset_id, status)
    std::map<std::string, MatchingEngineInfo> matching_engines;
    // client-details-provider key -> parsed info (addr, status)
    std::map<std::string, AddrStatusInfo> cdp_endpoints;
    // socket-gateway numeric id -> parsed info (addr, status). Numeric id
    // is assumed to double as GatewayId -- see etcd_watcher.hpp note.
    std::map<uint32_t, AddrStatusInfo> socket_gateways;

    void apply_etcd_event(const EtcdEvent &ev)
    {
        etcd_entries_touch(ev);

        switch (ev.resource_kind)
        {
        case EtcdResourceKind::MatchingEngine:
            if (ev.kind == EtcdEventKind::Delete)
                matching_engines.erase(ev.key);
            else if (ev.matching_engine)
                matching_engines[ev.key] = *ev.matching_engine;
            break;
        case EtcdResourceKind::ClientDetailsProvider:
            if (ev.kind == EtcdEventKind::Delete)
                cdp_endpoints.erase(ev.key);
            else if (ev.addr_status)
                cdp_endpoints[ev.key] = *ev.addr_status;
            break;
        case EtcdResourceKind::SocketGateway:
            if (!ev.id)
                break;
            if (ev.kind == EtcdEventKind::Delete)
                socket_gateways.erase(*ev.id);
            else if (ev.addr_status)
                socket_gateways[*ev.id] = *ev.addr_status;
            break;
        case EtcdResourceKind::Unknown:
            break;
        }
    }

private:
    void etcd_entries_touch(const EtcdEvent &ev)
    {
        if (ev.kind == EtcdEventKind::Delete)
            etcd_entries.erase(ev.key);
        else
            etcd_entries[ev.key] = ev.raw_value;
    }

public:
    // client_id -> live balance, for BOTH the two trading panels and any
    // ad-hoc client lookups.
    std::map<uint32_t, LiveBalance> balances;

    void push_trade(const TradeFeedEntry &e)
    {
        trade_feed.push_back(e);
        if (trade_feed.size() > kMaxTradeFeed)
            trade_feed.erase(trade_feed.begin(),
                             trade_feed.begin()
                                 + (trade_feed.size() - kMaxTradeFeed));
    }

    // Single entry point for an incoming OrderBookUpdate -- updates the
    // live book unconditionally (this part needs no timestamp and works
    // regardless of NAOTO_PERF), AND records the price/volume history
    // observation from the same event when timestamps are actually
    // available, so the two can never drift out of sync with each other
    // when they are. Without NAOTO_PERF, price_history simply never
    // gets populated -- the Price History page's existing "no data yet"
    // state handles that correctly with no special-casing needed there.
    void record_book_update(const naoto::OrderBookUpdate &u)
    {
        int64_t delta = order_books[u.AssetId].apply(u);
#ifdef NAOTO_PERF
        // Only depth DECREASES count toward the volume proxy -- see
        // AssetPriceHistory's comment for why increases don't.
        double volume_contribution =
            delta < 0 ? static_cast<double>(-delta) : 0.0;
        price_history[u.AssetId].record(u.UpdateTimestamp, u.Price,
                                        volume_contribution,
                                        price_history_bucket_ms);
#else
        (void)delta;
#endif
    }

    // Applies a report's effect to any client we're currently tracking a
    // balance for (i.e. one of the two trading panels, or a client
    // that's been looked up). Both legs of a match arrive as separate
    // OrderStateReports (one per client), so this is called once per
    // report, not once per match. Applied unconditionally regardless of
    // State (FILL/PARTIAL_FILL/CANCEL/REJECT/ADD) -- the deltas
    // themselves are defined to already be correct for whatever
    // happened (e.g. a REJECT presumably carries all-zero deltas, an ADD
    // reserves via SoldAttemptDelta without touching Confirmed yet); the
    // State only changes how this should be *described*, not whether the
    // numbers get applied. See draw_trade_feed_panel in main.cpp.
    void apply_trade_to_balance(const naoto::OrderStateReport &r)
    {
        auto it = balances.find(r.ClientId);
        if (it == balances.end())
            return; // not a client we're tracking right now
        it->second.apply_confirmed_delta(r.BoughtAssetId, r.BoughtDelta);
        it->second.apply_confirmed_delta(r.SoldAssetId, r.SoldDelta);
        it->second.apply_attempt_delta(r.SoldAssetId, r.SoldAttemptDelta);
    }
};