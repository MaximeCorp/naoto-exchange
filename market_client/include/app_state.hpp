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

#include <cstdint>
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

    void apply(const MarketExecution::OrderBookUpdate &u)
    {
        if (u.Side == MarketExecution::ORDER_BOOK_UPDATE_BUY)
        {
            if (u.Depth == 0)
                bids.erase(u.Price);
            else
                bids[u.Price] = u.Depth;
        }
        else
        {
            if (u.Depth == 0)
                asks.erase(u.Price);
            else
                asks[u.Price] = u.Depth;
        }
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
    MarketExecution::OrderState state; // FILL/PARTIAL_FILL/CANCEL/REJECT/ADD --
                                       // changes how this entry should read,
                                       // see draw_trade_feed_panel in main.cpp
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

    // Most recent N trades, newest last. Cap to avoid unbounded growth
    // over a long session.
    std::vector<TradeFeedEntry> trade_feed;
    static constexpr size_t kMaxTradeFeed = 2000;

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
    void apply_trade_to_balance(const MarketExecution::OrderStateReport &r)
    {
        auto it = balances.find(r.ClientId);
        if (it == balances.end())
            return; // not a client we're tracking right now
        it->second.apply_confirmed_delta(r.BoughtAssetId, r.BoughtDelta);
        it->second.apply_confirmed_delta(r.SoldAssetId, r.SoldDelta);
        it->second.apply_attempt_delta(r.SoldAssetId, r.SoldAttemptDelta);
    }
};