#pragma once
//
// trading_session.hpp
//
// One TradingSession = one TCP connection = one authenticated client
// session, matching "have to authenticate at each open connection with
// the key in the right binary." Two independent TradingSession instances
// give you the split-window "trade as two clients at once" requirement.
//
// Protocol on this connection:
//   1. connect()
//   2. send ClientRequest{'A', ClientId, Key}   -- first message, always
//   3. [ASSUMPTION -- confirm] no explicit ack for step 2 is expected;
//      the connection is presumed authenticated unless/until it's closed
//      by the server or every subsequent OrderConfirmation comes back
//      TechnicalFailure/UserNotConnected. If the real gateway does send
//      an ack here, this is the one place to add reading it.
//   4. send Order{...} any number of times
//   5. receive OrderConfirmation for each Order sent, on a background
//      reader thread, pushed to a queue the render thread drains
//   6. optionally send ClientRequest{'D', ClientId, Key} then close
//
// All I/O happens off the render thread. Nothing in here touches ImGui.
//

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "log.hpp"
#include "mpsc_queue.hpp"
#include "wire_formats.hpp"

enum class SessionState
{
    Disconnected,
    Connecting,
    Connected, // TCP connected, ClientRequest('A') sent
    Failed,
};

class TradingSession
{
public:
    explicit TradingSession(
        MpscQueue<Gateways::OrderConfirmation> &confirmations)
        : confirmations_(confirmations)
    {}

    ~TradingSession()
    {
        disconnect();
    }

    // Blocking connect + auth handshake. Call from a worker thread (e.g.
    // std::thread{[&]{ session.connect(...); }}.detach()), never from
    // the render thread -- connect() can stall on a dead host.
    void connect(const std::string &host, uint16_t port, uint32_t client_id,
                 const std::array<uint8_t, 32> &key)
    {
        state_.store(SessionState::Connecting);
        client_id_ = client_id;
        key_ = key;

        DASHBOARD_LOG("TradingSession", "connecting to %s:%u as client %u",
                      host.c_str(), port, client_id);

        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
        {
            state_.store(SessionState::Failed);
            set_last_error("socket() failed");
            DASHBOARD_LOG("TradingSession", "socket() failed: %s",
                          strerror(errno));
            return;
        }

        int nodelay = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
        {
            state_.store(SessionState::Failed);
            set_last_error("invalid host: " + host);
            DASHBOARD_LOG("TradingSession", "invalid host: %s", host.c_str());
            ::close(fd_);
            fd_ = -1;
            return;
        }

        if (::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))
            < 0)
        {
            state_.store(SessionState::Failed);
            set_last_error(std::string("connect() failed: ") + strerror(errno));
            DASHBOARD_LOG("TradingSession", "connect() failed: %s",
                          strerror(errno));
            ::close(fd_);
            fd_ = -1;
            return;
        }

        DASHBOARD_LOG("TradingSession",
                      "TCP connected (fd=%d), sending ClientRequest",
                      fd_.load());

        Gateways::ClientRequest req{};
        req.RequestType = 'A';
        req.ClientId = client_id_;
        req.Key = key_;

        DASHBOARD_LOG(
            "TradingSession",
            "ClientRequest: RequestType='%c' ClientId=%u sizeof=%zu bytes=%s",
            req.RequestType, req.ClientId, sizeof(req),
            hex_dump(reinterpret_cast<const uint8_t *>(&req), sizeof(req))
                .c_str());

        if (!send_all(reinterpret_cast<const uint8_t *>(&req), sizeof(req)))
        {
            state_.store(SessionState::Failed);
            set_last_error("failed sending ClientRequest");
            DASHBOARD_LOG("TradingSession", "send() failed: %s",
                          strerror(errno));
            ::close(fd_);
            fd_ = -1;
            return;
        }

        // The gateway sends no ack for this handshake. Its request-send
        // thread polls two queues (blocking, 75ms timeout each), so a
        // message sent immediately after this one isn't guaranteed to be
        // processed in order yet. Per project guidance: wait at least
        // 100ms before treating the session as ready for orders. This is
        // exactly what gates send_order() below (it checks
        // state_==Connected, which we only set after this sleep).
        DASHBOARD_LOG(
            "TradingSession",
            "ClientRequest sent, waiting 100ms before allowing orders");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        state_.store(SessionState::Connected);
        DASHBOARD_LOG("TradingSession", "session ready (client %u)",
                      client_id_);
        running_.store(true);
        reader_thread_ = std::thread([this] { read_loop(); });
    }

    // Thread-safe: sends immediately on the session's socket. Since one
    // TradingSession belongs to one client/window and orders are placed
    // one at a time from user input, no separate outbound queue is
    // needed -- send() itself is the serialization point. If you later
    // want to fire orders programmatically from multiple threads, add
    // an outbound MpscQueue<Order> drained by reader_loop (or a second
    // writer thread) instead of calling send_order() concurrently.
    bool send_order(uint32_t local_order_id, Gateways::OrderType type,
                    Gateways::OrderSide side, int64_t price, uint32_t amount,
                    int32_t asset)
    {
        if (state_.load() != SessionState::Connected)
        {
            DASHBOARD_LOG(
                "TradingSession",
                "send_order() rejected locally: session not Connected");
            return false;
        }

        Gateways::Order order{};
        order.Price = price;
        order.OrderId = local_order_id;
        order.ClientOrderId =
            local_order_id; // see ASSUMPTION note in wire_formats.hpp
        order.ClientId = client_id_;
        order.Amount = amount;
        order.AssetId = static_cast<uint16_t>(std::clamp(asset, 0, 65535));
        order.Type = type;
        order.Side = side;
        order.Action = Gateways::OrderAction::EXECUTE;

        DASHBOARD_LOG("TradingSession",
                      "sending Order orderId=%u client=%u asset=%u side=%d "
                      "type=%d price=%lld amount=%u",
                      local_order_id, client_id_, order.AssetId,
                      static_cast<int>(side), static_cast<int>(type),
                      (long long)price, amount);
        DASHBOARD_LOG(
            "TradingSession", "Order raw bytes (%zu): %s", sizeof(order),
            hex_dump(reinterpret_cast<const uint8_t *>(&order), sizeof(order))
                .c_str());

        bool ok =
            send_all(reinterpret_cast<const uint8_t *>(&order), sizeof(order));
        if (!ok)
            DASHBOARD_LOG("TradingSession", "send(Order) failed: %s",
                          strerror(errno));
        return ok;
    }

    // Cancels an existing order. Action::CANCEL repurposes Amount to
    // carry the target OrderId, per wire_formats.hpp's Order struct
    // comment. Whether Price/Type/Side matter for a cancel isn't
    // confirmed either way; left at harmless defaults rather than
    // whatever the order form happened to have, since a cancel isn't
    // conceptually tied to the form's current side/price selection.
    bool send_cancel_order(uint32_t local_order_id, uint32_t order_id_to_cancel,
                           int32_t asset)
    {
        if (state_.load() != SessionState::Connected)
        {
            DASHBOARD_LOG(
                "TradingSession",
                "send_cancel_order() rejected locally: session not Connected");
            return false;
        }

        Gateways::Order order{};
        order.Price = 0;
        order.OrderId = local_order_id;
        order.ClientOrderId = local_order_id;
        order.ClientId = client_id_;
        order.Amount =
            order_id_to_cancel; // target order id -- see struct comment
        order.AssetId = static_cast<uint16_t>(std::clamp(asset, 0, 65535));
        order.Type = Gateways::OrderType::LIMIT;
        order.Side = Gateways::OrderSide::BUY;
        order.Action = Gateways::OrderAction::CANCEL;

        DASHBOARD_LOG("TradingSession",
                      "sending CANCEL for order %u (client %u, asset %u)",
                      order_id_to_cancel, client_id_, order.AssetId);
        DASHBOARD_LOG(
            "TradingSession", "Cancel Order raw bytes (%zu): %s", sizeof(order),
            hex_dump(reinterpret_cast<const uint8_t *>(&order), sizeof(order))
                .c_str());

        bool ok =
            send_all(reinterpret_cast<const uint8_t *>(&order), sizeof(order));
        if (!ok)
            DASHBOARD_LOG("TradingSession", "send(Cancel Order) failed: %s",
                          strerror(errno));
        return ok;
    }

    void disconnect()
    {
        if (state_.load() == SessionState::Connected && fd_.load() >= 0)
        {
            Gateways::ClientRequest req{};
            req.RequestType = 'D';
            req.ClientId = client_id_;
            req.Key = key_;
            send_all(reinterpret_cast<const uint8_t *>(&req), sizeof(req));
        }
        running_.store(false);
        // Only shutdown() here -- NEVER close() from this thread. If
        // read_loop() is still blocked inside recv() on this fd (very
        // likely, since it blocks waiting for confirmations), closing
        // the fd concurrently from another thread is undefined behavior
        // at the POSIX level regardless of how fd_ itself is
        // synchronized -- shutdown() is the documented-safe way to
        // unblock a peer thread's blocking read. read_loop() is the
        // sole owner of the actual close(), done right after its own
        // recv() call returns (see bottom of read_loop()).
        int fd_snapshot = fd_.load();
        if (fd_snapshot >= 0)
            ::shutdown(fd_snapshot, SHUT_RDWR);
        if (reader_thread_.joinable())
            reader_thread_.join();
        state_.store(SessionState::Disconnected);
    }

    SessionState state() const
    {
        return state_.load();
    }
    std::string last_error() const
    {
        std::lock_guard<std::mutex> lock(last_error_mutex_);
        return last_error_;
    }
    uint32_t client_id() const
    {
        return client_id_;
    }

private:
    bool send_all(const uint8_t *data, size_t len)
    {
        size_t sent = 0;
        while (sent < len)
        {
            ssize_t n = ::send(fd_, data + sent, len - sent, 0);
            if (n <= 0)
                return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    bool recv_all(uint8_t *data, size_t len)
    {
        size_t got = 0;
        while (got < len)
        {
            ssize_t n = ::recv(fd_, data + got, len - got, 0);
            if (n <= 0)
                return false; // closed or error
            got += static_cast<size_t>(n);
        }
        return true;
    }

    void read_loop()
    {
        while (running_.load(std::memory_order_relaxed))
        {
            Gateways::OrderConfirmation conf{};
            if (!recv_all(reinterpret_cast<uint8_t *>(&conf), sizeof(conf)))
            {
                if (running_.load(std::memory_order_relaxed))
                {
                    // Unexpected disconnect -- surface it via state, the
                    // render thread will see Failed and show it.
                    state_.store(SessionState::Failed);
                    set_last_error("connection closed by peer");
                    DASHBOARD_LOG("TradingSession",
                                  "connection closed by peer (client %u)",
                                  client_id_);
                }
                break;
            }
            DASHBOARD_LOG(
                "TradingSession",
                "OrderConfirmation: OrderId=%u ClientOrderId=%u Status=%s",
                conf.OrderId, conf.ClientOrderId,
                Gateways::ToString(conf.Status));
            confirmations_.push(conf); // safe: render thread only drains
        }

        // This thread is the sole owner of closing the fd, done only
        // once its own recv() has actually returned -- see disconnect(),
        // which deliberately only shutdown()s and never close()s.
        int fd_snapshot = fd_.exchange(-1);
        if (fd_snapshot >= 0)
            ::close(fd_snapshot);
    }

    void set_last_error(std::string msg)
    {
        std::lock_guard<std::mutex> lock(last_error_mutex_);
        last_error_ = std::move(msg);
    }

    static int64_t now_ms()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
                   system_clock::now().time_since_epoch())
            .count();
    }

    MpscQueue<Gateways::OrderConfirmation> &confirmations_;
    std::atomic<int> fd_ = -1;
    uint32_t client_id_ = 0;
    std::array<uint8_t, 32> key_{};
    std::atomic<SessionState> state_{ SessionState::Disconnected };
    std::atomic<bool> running_{ false };
    std::thread reader_thread_;
    mutable std::mutex last_error_mutex_;
    std::string last_error_; // guarded by last_error_mutex_ -- written from
                             // both connect() (its own detached thread)
                             // and read_loop() (reader_thread_), read from
                             // the render thread via last_error()
};