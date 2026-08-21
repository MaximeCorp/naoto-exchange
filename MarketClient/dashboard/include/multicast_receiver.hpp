#pragma once
//
// multicast_receiver.hpp
//
// Plain kernel UDP multicast socket receiver. The production system uses
// DPDK kernel-bypass for this feed on the hot path; this dashboard is a
// monitoring/UI consumer refreshing at human-observable rates, so a
// normal socket is simpler and entirely sufficient.
//
// IMPORTANT: this receiver's own path is a normal kernel socket
// regardless of how the sender transmits (DPDK or otherwise) -- so
// same-host loopback caveats below apply to it even when the publisher
// is a DPDK app that itself bypasses the kernel.
//
// Same-host testing notes:
//   - Interface selection matters. By default we join on INADDR_ANY and
//     let the kernel pick an interface, which may NOT be the interface
//     the traffic actually arrives on -- e.g. in a DPDK net_af veth-pair
//     test setup (sender on veth0, traffic landing on veth1), the
//     receiver needs to explicitly join on veth1 or it may simply never
//     see the packets, independent of any rp_filter question. Pass
//     local_interface_name to pin this.
//   - Interface selection is done by INDEX (if_nametoindex), not by IP
//     address. veth interfaces used purely for raw/multicast traffic
//     often have no IPv4 address assigned at all, which would make the
//     older IP-address-based join (ip_mreq / SIOCGIFADDR) fail with
//     EADDRNOTAVAIL -- "Cannot assign requested address" -- even though
//     the interface is perfectly usable. Using ip_mreqn with imr_ifindex
//     avoids that; it doesn't require the interface to have an IP.
//   - We set SO_REUSEADDR (and SO_REUSEPORT where available) so multiple
//     local processes/sockets can bind the same mcast port.
//   - We do NOT set IP_MULTICAST_LOOP here -- that flag governs whether
//     the SENDER loops packets back locally, it's a sender-side socket
//     option (and doesn't apply at all when the sender is a DPDK app
//     that bypasses the kernel's multicast loop mechanism entirely --
//     its packets go straight onto the wire/veth).
//   - If packets are still being silently dropped despite binding the
//     right interface, it's often Linux reverse-path filtering rejecting
//     a multicast packet whose source looks "wrong" for the interface it
//     arrived on. Fix on the test box (not something this code can do
//     for you):
//       sudo sysctl -w net.ipv4.conf.all.rp_filter=0
//       sudo sysctl -w net.ipv4.conf.veth1.rp_filter=0   (or whichever
//                                                          interface is
//                                                          actually used)
//

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <functional>
#include <net/if.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "log.hpp"

class MulticastReceiver
{
public:
    // on_packet is called from the receiver's OWN background thread.
    // It must NOT touch ImGui state directly -- push into an MpscQueue
    // instead, and drain that queue from the render thread.
    using PacketHandler = std::function<void(const uint8_t *data, size_t len)>;

    // local_interface_name: e.g. "veth1" to pin which interface the
    // group is joined on. Empty string = INADDR_ANY (let the kernel
    // choose) -- fine on a box with a single relevant interface, risky
    // otherwise (see file header note about veth-pair test setups).
    MulticastReceiver(std::string group_addr, uint16_t port,
                      PacketHandler handler,
                      std::string local_interface_name = "")
        : group_addr_(std::move(group_addr))
        , port_(port)
        , handler_(std::move(handler))
        , local_interface_name_(std::move(local_interface_name))
    {}

    ~MulticastReceiver()
    {
        stop();
    }

    void start()
    {
        if (running_.exchange(true))
            return;

        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0)
            throw std::runtime_error("MulticastReceiver: socket() failed: "
                                     + std::string(strerror(errno)));

        int reuse = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port_);

        if (::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            ::close(fd_);
            throw std::runtime_error("MulticastReceiver: bind() failed on port "
                                     + std::to_string(port_) + ": "
                                     + strerror(errno));
        }

        int iface_index = 0; // 0 = kernel picks (matches INADDR_ANY behavior)
        if (!local_interface_name_.empty())
        {
            iface_index = static_cast<int>(
                ::if_nametoindex(local_interface_name_.c_str()));
            if (iface_index == 0)
            {
                ::close(fd_);
                throw std::runtime_error(
                    "MulticastReceiver: interface '" + local_interface_name_
                    + "' not found (if_nametoindex failed): "
                    + strerror(errno));
            }
        }

        // ip_mreqn (not ip_mreq) lets us select the interface by index
        // (imr_ifindex) instead of requiring it to have an IPv4 address
        // assigned -- veth interfaces used purely for raw/multicast
        // traffic often have none. The kernel distinguishes which struct
        // was passed by the optlen given to setsockopt.
        ip_mreqn mreq{};
        mreq.imr_multiaddr.s_addr = inet_addr(group_addr_.c_str());
        mreq.imr_address.s_addr = htonl(INADDR_ANY);
        mreq.imr_ifindex = iface_index;
        if (setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))
            < 0)
        {
            ::close(fd_);
            throw std::runtime_error(
                "MulticastReceiver: IP_ADD_MEMBERSHIP failed for " + group_addr_
                + (local_interface_name_.empty()
                       ? ""
                       : " on interface " + local_interface_name_)
                + ": " + strerror(errno));
        }

        thread_ = std::thread([this] { run(); });

        DASHBOARD_LOG(
            "MulticastReceiver",
            "listening on %s:%u%s (waiting for first packet...)",
            group_addr_.c_str(), port_,
            local_interface_name_.empty()
                ? ""
                : (" via interface " + local_interface_name_).c_str());
    }

    void stop()
    {
        if (!running_.exchange(false))
            return;
        if (fd_ >= 0)
        {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
        if (thread_.joinable())
            thread_.join();
    }

private:
    void run()
    {
        std::vector<uint8_t> buf(65536);
        bool logged_first_packet = false;
        while (running_.load(std::memory_order_relaxed))
        {
            ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
            if (n <= 0)
            {
                if (!running_.load(std::memory_order_relaxed))
                    break; // shutdown() unblocked recv() intentionally
                continue; // transient error, keep going
            }
            if (!logged_first_packet)
            {
                DASHBOARD_LOG("MulticastReceiver",
                              "first packet received on %s:%u (%zd bytes)",
                              group_addr_.c_str(), port_, n);
                logged_first_packet = true;
            }
            handler_(buf.data(), static_cast<size_t>(n));
        }
    }

    std::string group_addr_;
    uint16_t port_;
    PacketHandler handler_;
    std::string local_interface_name_;
    int fd_ = -1;
    std::atomic<bool> running_{ false };
    std::thread thread_;
};