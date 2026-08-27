#pragma once
//
// client_details_poller.hpp
//
// Direct dashboard <-> CDP connection (a SEPARATE protocol/connection
// from the trading gateway connection in trading_session.hpp). Confirmed
// protocol:
//
//   1. connect to CDP's TCP addr (from etcd /client-details-provider/<id>)
//   2. send GatewayConnection{GatewayId}        -- always first
//   3. send GatewayRequest{'A', ClientId, Key, ClientFd, GatewayId}
//   4. receive ClientRequestResponse<MaxPositions>
//
// "GatewayId must match the client's authorized value for the request to
// be accepted" -- i.e. we're impersonating whichever real gateway that
// client is assigned to. With a single test gateway registered as
// /socket-gateways/0, GatewayId=0 works for every client. OPEN QUESTION:
// once there's more than one gateway, what determines which GatewayId is
// authorized for a given client? Not modeled in etcd today. Until that's
// answered, the caller supplies gateway_id explicitly (defaulted to
// whatever's discovered in etcd -- see main.cpp) rather than this file
// guessing.
//
// ClientFd: per the struct comment this is "information given by
// gateway" -- since we're impersonating the gateway rather than being
// it, we don't have a real client fd to report. We generate a unique
// value per query (see the static counter in query()) rather than
// hardcoding 0, since hardcoding it risked CDP conflating concurrent
// impersonated connections that shared the same (GatewayId, ClientFd)
// pair -- this was a real symptom (balances "toggling" between two
// clients) before the design changed to query-once-then-listen below.
//
// "Real-time" balance strategy: CDP has no push/subscription for balance
// changes, and repeatedly re-querying it in a loop caused problems (see
// above) -- so this is now a ONE-SHOT query, called once right after a
// trading connection is authenticated (see main.cpp), after which the
// balance is kept live purely by applying OrderStateReport deltas from
// the multicast feed (AppState::apply_trade_to_balance). No periodic
// reconciliation poll. If drift is ever observed, a manual re-query via
// the Client Lookup panel updates the same balance state.
//

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cstring>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <tuple>
#include <unistd.h>
#include <vector>

#include "log.hpp"
#include "wire_formats.hpp"

// Adjust to the deployed CDP MaxPositions before trusting results.
constexpr size_t CDP_MAX_POSITIONS = 16;
using ClientResponse = Gateways::ClientRequestResponse<CDP_MAX_POSITIONS>;

struct ClientBalanceSnapshot
{
    uint32_t client_id;
    bool ok = false;
    std::string error;
    // asset_id -> (confirmed, attempt); asset_id 0 is the dollar/cash
    // balance and should be shown distinctly per the spec.
    std::vector<std::tuple<uint16_t, int64_t, int64_t>> positions;
};

class ClientDetailsPoller
{
public:
    // One-shot blocking query against CDP directly. Call from a worker
    // thread, push the result into an MpscQueue<ClientBalanceSnapshot>
    // for the render thread to consume -- never call this from the
    // render thread.
    //
    // cdp_host/cdp_port: CDP's own address (from etcd
    // /client-details-provider/<id>), NOT the trading gateway's address.
    // gateway_id: the GatewayId we impersonate for this client.
    static ClientBalanceSnapshot query(const std::string &cdp_host,
                                       uint16_t cdp_port, uint32_t client_id,
                                       const std::array<uint8_t, 32> &key,
                                       uint16_t gateway_id)
    {
        ClientBalanceSnapshot result;
        result.client_id = client_id;

        // Unique per query, not hardcoded 0. Every "gateway" (real or, as
        // here, impersonated) opening a connection to CDP under the same
        // GatewayId with the same ClientFd risks CDP conflating two
        // concurrent impersonated connections -- e.g. two trading panels
        // both polling with GatewayId=0, ClientFd=0 simultaneously could
        // get each other's responses mixed up. This alone doesn't fully
        // solve that (each panel now only queries once at connect time
        // rather than in a loop -- see main.cpp -- which is the real
        // fix), but it removes one avoidable source of collision.
        static std::atomic<uint32_t> next_client_fd{ 1 };
        uint32_t client_fd = next_client_fd.fetch_add(1);

        DASHBOARD_LOG("CDP", "querying %s:%u for client %u (gateway %u, fd %u)",
                      cdp_host.c_str(), cdp_port, client_id, gateway_id,
                      client_fd);

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            result.error = "socket() failed";
            DASHBOARD_LOG("CDP", "socket() failed: %s", strerror(errno));
            return result;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cdp_port);
        if (inet_pton(AF_INET, cdp_host.c_str(), &addr.sin_addr) != 1)
        {
            result.error = "invalid CDP host: " + cdp_host;
            DASHBOARD_LOG("CDP", "invalid host: %s", cdp_host.c_str());
            ::close(fd);
            return result;
        }

        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))
            < 0)
        {
            result.error =
                std::string("connect() to CDP failed: ") + strerror(errno);
            DASHBOARD_LOG("CDP", "connect() failed: %s", strerror(errno));
            ::close(fd);
            return result;
        }

        // 1. GatewayConnection -- always first message to CDP.
        Gateways::GatewayConnection conn{};
        conn.GatewayId = gateway_id;
        if (!send_all(fd, reinterpret_cast<const uint8_t *>(&conn),
                      sizeof(conn)))
        {
            result.error = "failed sending GatewayConnection";
            DASHBOARD_LOG("CDP", "send(GatewayConnection) failed: %s",
                          strerror(errno));
            ::close(fd);
            return result;
        }

        // 2. GatewayRequest -- the actual client-details query.
        Gateways::GatewayRequest req{};
        req.RequestType = 'A';
        req.ClientId = client_id;
        req.Key = key;
        req.ClientFd = client_fd;
        req.GatewayId = gateway_id;

        if (!send_all(fd, reinterpret_cast<const uint8_t *>(&req), sizeof(req)))
        {
            result.error = "failed sending GatewayRequest";
            DASHBOARD_LOG("CDP", "send(GatewayRequest) failed: %s",
                          strerror(errno));
            ::close(fd);
            return result;
        }

        // 3. ClientRequestResponse.
        ClientResponse resp{};
        if (!recv_all(fd, reinterpret_cast<uint8_t *>(&resp), sizeof(resp)))
        {
            result.error = "no/short response from CDP (check MaxPositions)";
            DASHBOARD_LOG("CDP", "recv(ClientRequestResponse) failed/short");
            ::close(fd);
            return result;
        }
        ::close(fd);

        DASHBOARD_LOG("CDP",
                      "response for client %u: Status='%c' RespClientId=%u",
                      client_id, resp.Status, resp.ClientId);

        if (resp.Status != 'A')
        {
            result.error = resp.Status == 'C'
                ? "wrong credentials"
                : "refused (check GatewayId is authorized "
                  "for this client)";
            return result;
        }

        result.ok = true;
        for (size_t i = 0; i < CDP_MAX_POSITIONS; ++i)
            result.positions.emplace_back(resp.AssetId[i], resp.Confirmed[i],
                                          resp.Attempt[i]);
        return result;
    }

private:
    static bool send_all(int fd, const uint8_t *data, size_t len)
    {
        size_t sent = 0;
        while (sent < len)
        {
            ssize_t n = ::send(fd, data + sent, len - sent, 0);
            if (n <= 0)
                return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    static bool recv_all(int fd, uint8_t *data, size_t len)
    {
        size_t got = 0;
        while (got < len)
        {
            ssize_t n = ::recv(fd, data + got, len - got, 0);
            if (n <= 0)
                return false;
            got += static_cast<size_t>(n);
        }
        return true;
    }
};