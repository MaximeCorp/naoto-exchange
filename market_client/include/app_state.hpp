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
#include <chrono>
#include <cmath>
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
    // didn't previously exist).
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

// ---- Wall-clock receive stamping ------------------------------------------
// Price history is plotted against the time THIS dashboard received each
// message, not against any timestamp carried on the wire. The wire
// timestamps (NAOTO_PERF only) are raw RDTSC cycles from the sender's
// machine -- no epoch, unknown frequency -- so they can't be placed on a
// clock axis. The previous history code fed those cycle counts straight
// into a "milliseconds" bucketing function, which put every update in its
// own sub-microsecond "candle" (open == close, zero-height body) on an
// axis millions of "seconds" wide: the flat chart. And without NAOTO_PERF
// (the default build) it recorded nothing at all.
//
// Stamped on the network thread, at recv() time, so a frame's worth of
// batched messages still keep their real spacing.
inline double wall_clock_seconds()
{
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

template <typename T>
struct Stamped
{
    double recv_time; // seconds since the Unix epoch, see wall_clock_seconds()
    T msg;
};

// ---- Price history: real trades + top of book ------------------------------
// One executed trade, decoded from the order-state feed. The matching
// engine (market_server/matching_engine/include/bid_ask.hpp, FillBuyOrder/
// FillSellOrder) emits TWO reports per match, one per counterparty, and
// neither carries a price field directly -- but the cash leg does:
//
//   buyer : Bought=<asset>, BoughtDelta=+qty,  Sold=0, SoldDelta=-qty*price
//   seller: Bought=0, BoughtDelta=+qty*price, Sold=<asset>, SoldDelta=-qty
//
// TradeId is always 0 on the wire, so it can't be used to pair the two
// legs. Instead only the BUYER leg is taken (exactly one per match,
// whichever side was the aggressor), and price = cash / qty, which is
// exact since the engine computes the cash leg as qty * level price.
struct TradePrint
{
    double time; // receive time, epoch seconds
    int64_t price;
    uint32_t qty;
};

// Best bid/ask after a book update changed either of them. 0 = that side
// of the book is empty.
struct QuoteSample
{
    double time;
    int64_t bid;
    int64_t ask;
};

struct Candle
{
    double start; // bucket start, epoch seconds
    double open = 0, high = 0, low = 0, close = 0;
    double volume = 0; // traded quantity, real fills only
    uint32_t trades = 0;
};

// Returns true (and fills asset/price/qty) if `r` is the buyer leg of a
// fill -- see TradePrint for why only that leg counts.
inline bool decode_trade(const naoto::OrderStateReport &r, uint16_t &asset,
                         int64_t &price, uint32_t &qty)
{
    if (r.State != naoto::OrderState::FILL
        && r.State != naoto::OrderState::PARTIAL_FILL)
        return false;
    if (r.BoughtAssetId == 0 || r.SoldAssetId != 0 || r.BoughtDelta <= 0
        || r.SoldDelta >= 0)
        return false;
    asset = r.BoughtAssetId;
    qty = static_cast<uint32_t>(r.BoughtDelta);
    price = -r.SoldDelta / r.BoughtDelta;
    return true;
}

struct AssetMarketHistory
{
    // Raw data is kept and candles are rebuilt from it on demand, so
    // switching timeframe re-buckets the whole session instead of
    // throwing history away. Capped to bound memory on long sessions.
    std::deque<TradePrint> trades; // oldest first
    std::deque<QuoteSample> quotes; // oldest first
    static constexpr size_t kMaxTrades = 200000;
    static constexpr size_t kMaxQuotes = 200000;

    void add_trade(double time, int64_t price, uint32_t qty)
    {
        trades.push_back({ time, price, qty });
        if (trades.size() > kMaxTrades)
            trades.pop_front();
    }

    // Only records when the top of book actually moved -- most depth
    // updates are behind the touch and don't change what gets plotted.
    void add_quote(double time, int64_t bid, int64_t ask)
    {
        if (!quotes.empty() && quotes.back().bid == bid
            && quotes.back().ask == ask)
            return;
        quotes.push_back({ time, bid, ask });
        if (quotes.size() > kMaxQuotes)
            quotes.pop_front();
    }

    // Buckets with no trades are skipped (left as gaps) rather than
    // carried forward, so an idle market reads as idle.
    std::vector<Candle> build_candles(double bucket_s) const
    {
        std::vector<Candle> out;
        if (bucket_s <= 0.0)
            return out;
        for (const TradePrint &t : trades)
        {
            double start = std::floor(t.time / bucket_s) * bucket_s;
            double p = static_cast<double>(t.price);
            if (out.empty() || out.back().start != start)
            {
                Candle c;
                c.start = start;
                c.open = c.high = c.low = c.close = p;
                out.push_back(c);
            }
            Candle &c = out.back();
            c.high = std::max(c.high, p);
            c.low = std::min(c.low, p);
            c.close = p;
            c.volume += t.qty;
            c.trades += 1;
        }
        return out;
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

    // asset_id -> trade prints + top-of-book samples for the Price History
    // page. See AssetMarketHistory / TradePrint for where each comes from.
    std::map<uint16_t, AssetMarketHistory> market_history;

    // Raw message counters, shown on the Price History page so "is any
    // data arriving at all?" can be answered without a debugger.
    uint64_t book_updates_received = 0;
    uint64_t order_reports_received = 0;
    uint64_t trades_decoded = 0;

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

    // Single entry point for an incoming OrderBookUpdate: applies it to
    // the live book, then samples the resulting best bid/ask for the
    // Price History page.
    void record_book_update(const naoto::OrderBookUpdate &u, double recv_time)
    {
        ++book_updates_received;
        OrderBook &book = order_books[u.AssetId];
        book.apply(u);
        int64_t bid = book.bids.empty() ? 0 : book.bids.begin()->first;
        int64_t ask = book.asks.empty() ? 0 : book.asks.begin()->first;
        market_history[u.AssetId].add_quote(recv_time, bid, ask);
    }

    // Records the trade (if any) carried by an order-state report. Called
    // for every report; decode_trade() filters to one print per match.
    void record_order_report(const naoto::OrderStateReport &r, double recv_time)
    {
        ++order_reports_received;
        uint16_t asset;
        int64_t price;
        uint32_t qty;
        if (!decode_trade(r, asset, price, qty))
            return;
        ++trades_decoded;
        market_history[asset].add_trade(recv_time, price, qty);
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