#pragma once
//
// etcd_watcher.hpp
//
// Watches three known etcd prefixes for live service discovery, confirmed
// against a real running instance:
//
//   /matching-engines/<engine_id>        {"addr":"...","asset_id":N,"status":"..."}
//   /client-details-provider/<cdp_id>    {"addr":"...","status":"..."}
//   /socket-gateways/<gateway_id>        {"ip":"...","status":"..."}
//
// Note socket-gateways uses "ip" where the other two use "addr" -- kept
// as-is rather than normalized, since that's the real key name.
//
// The numeric suffix on the key (e.g. "0" in "/socket-gateways/0") is
// treated as that resource's ID -- for socket-gateways specifically,
// this is assumed to be the GatewayId used in GatewayConnection /
// GatewayRequest. Confirmed by the one-gateway test setup (only gateway
// running was registered as .../0, matching GatewayId 0 conventions
// elsewhere); re-verify once there's more than one gateway to distinguish
// "key suffix == GatewayId" from coincidence.
//
// Entries are (believed to be) lease/TTL-backed, so "what's currently
// under a prefix" is itself a live health signal -- no separate
// staleness check needed once that's confirmed against the real
// deployment's lease/keepalive behavior.
//
// Threading: etcd::Watcher callbacks run on a library-managed background
// thread pool (cpprestsdk's pplx tasks), NOT a thread we control. We
// never touch ImGui state from that callback -- we push into an
// MpscQueue drained by the render thread, same pattern as everything
// else here.
//

#include "mpsc_queue.hpp"

#include <etcd/Client.hpp>
#include <etcd/Response.hpp>
#include <etcd/Watcher.hpp>

#include <nlohmann/json.hpp>

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class EtcdResourceKind
{
    MatchingEngine,
    ClientDetailsProvider,
    SocketGateway,
    Unknown,
};

struct MatchingEngineInfo
{
    std::string addr;
    uint16_t asset_id = 0;
    std::string status;
};

struct AddrStatusInfo // shared shape for CDP and socket-gateway entries
{
    std::string addr; // "addr" for CDP, "ip" for socket-gateways
    std::string status;
};

enum class EtcdEventKind
{
    Snapshot, // result of the initial ls()/get-prefix
    Put,
    Delete,
};

struct EtcdEvent
{
    EtcdEventKind kind;
    EtcdResourceKind resource_kind;
    std::string key;            // full key, e.g. "/socket-gateways/0"
    std::optional<uint32_t> id; // numeric suffix, if the key parses as one
    std::string raw_value;

    // Populated based on resource_kind; only the relevant one is set.
    std::optional<MatchingEngineInfo> matching_engine;
    std::optional<AddrStatusInfo> addr_status; // CDP or socket-gateway
};

namespace detail
{
inline EtcdResourceKind classify(const std::string &key)
{
    if (key.rfind("/matching-engines/", 0) == 0)
        return EtcdResourceKind::MatchingEngine;
    if (key.rfind("/client-details-provider/", 0) == 0)
        return EtcdResourceKind::ClientDetailsProvider;
    if (key.rfind("/socket-gateways/", 0) == 0)
        return EtcdResourceKind::SocketGateway;
    return EtcdResourceKind::Unknown;
}

inline std::optional<uint32_t> parse_trailing_id(const std::string &key)
{
    auto pos = key.find_last_of('/');
    if (pos == std::string::npos || pos + 1 >= key.size())
        return std::nullopt;
    std::string suffix = key.substr(pos + 1);
    uint32_t value = 0;
    auto res = std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
    if (res.ec != std::errc())
        return std::nullopt;
    return value;
}

inline void fill_parsed_fields(EtcdEvent &ev)
{
    try
    {
        auto j = nlohmann::json::parse(ev.raw_value);
        switch (ev.resource_kind)
        {
        case EtcdResourceKind::MatchingEngine:
        {
            MatchingEngineInfo info;
            info.addr = j.value("addr", "");
            info.asset_id = j.value("asset_id", 0);
            info.status = j.value("status", "");
            ev.matching_engine = info;
            break;
        }
        case EtcdResourceKind::ClientDetailsProvider:
        {
            AddrStatusInfo info;
            info.addr = j.value("addr", "");
            info.status = j.value("status", "");
            ev.addr_status = info;
            break;
        }
        case EtcdResourceKind::SocketGateway:
        {
            AddrStatusInfo info;
            info.addr = j.value("ip", ""); // note: "ip" not "addr" here
            info.status = j.value("status", "");
            ev.addr_status = info;
            break;
        }
        case EtcdResourceKind::Unknown:
            break;
        }
    }
    catch (const nlohmann::json::parse_error &)
    {
        // Malformed value -- leave the optional fields unset; UI shows
        // raw_value either way so nothing is silently lost.
    }
}
} // namespace detail

class EtcdResourceWatcher
{
public:
    EtcdResourceWatcher(std::string etcd_endpoint, MpscQueue<EtcdEvent> &events)
        : client_(std::move(etcd_endpoint)), events_(events)
    {
    }

    static constexpr const char *kPrefixes[] = {
        "/matching-engines/",
        "/client-details-provider/",
        "/socket-gateways/",
    };

    // Blocking: fetch the current set for every known prefix. Call from
    // a worker thread once at startup, before start_watch().
    void load_initial_snapshot()
    {
        for (const char *prefix : kPrefixes)
        {
            etcd::Response resp = client_.ls(prefix).get();
            if (!resp.is_ok())
                continue; // surfaced as "nothing discovered yet" in the UI

            for (auto const &kv : resp.values())
                events_.push(make_event(EtcdEventKind::Snapshot, kv.key(), kv.as_string()));
        }
    }

    // Starts one recursive watch per prefix. Runs until stop() / destruction.
    void start_watch()
    {
        for (const char *prefix : kPrefixes)
        {
            watchers_.push_back(std::make_unique<etcd::Watcher>(
                client_, prefix,
                [this](etcd::Response resp) { on_watch_response(resp); },
                /*recursive=*/true));
        }
    }

    void stop()
    {
        watchers_.clear();
    }

private:
    EtcdEvent make_event(EtcdEventKind kind, const std::string &key,
                          const std::string &value)
    {
        EtcdEvent ev;
        ev.kind = kind;
        ev.key = key;
        ev.raw_value = value;
        ev.resource_kind = detail::classify(key);
        ev.id = detail::parse_trailing_id(key);
        detail::fill_parsed_fields(ev);
        return ev;
    }

    void on_watch_response(const etcd::Response &resp)
    {
        // Do NOT touch ImGui here -- this runs on cpprestsdk's pplx
        // thread pool, not the render thread.
        if (!resp.is_ok())
            return;

        for (auto const &ev : resp.events())
        {
            EtcdEventKind kind;
            switch (ev.event_type())
            {
            case etcd::Event::EventType::PUT:
                kind = EtcdEventKind::Put;
                break;
            case etcd::Event::EventType::DELETE_:
                kind = EtcdEventKind::Delete;
                break;
            default:
                continue;
            }
            events_.push(make_event(kind, ev.kv().key(), ev.kv().as_string()));
        }
    }

    etcd::Client client_;
    MpscQueue<EtcdEvent> &events_;
    std::vector<std::unique_ptr<etcd::Watcher>> watchers_;
};
