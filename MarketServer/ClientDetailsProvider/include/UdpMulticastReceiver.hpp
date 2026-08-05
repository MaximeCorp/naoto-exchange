#pragma once

#include <ReaderWriterCircularBuffer.hpp>
#include <SequenceRingBuffer.hpp>
#include <StoragePool.hpp>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <string>
#include <variant>

namespace AccountService
{
    static void ipv4_multicast_to_mac(uint32_t ip_host_order,
                                      rte_ether_addr *mac);

    template <typename T>
    concept HasSequenceIdLocal = requires(T t) {
        { t.SequenceId } -> std::convertible_to<uint32_t>;
    };

    template <typename T, size_t RingBufferSize, size_t BatchSize = 0,
              size_t MTU = 1500>
        requires HasSequenceIdLocal<T>
    class UdpMulticastReceiver
    {
        static constexpr size_t ObjectsPerPacket =
            (MTU - sizeof(rte_ipv4_hdr) - sizeof(rte_udp_hdr)) / sizeof(T);

        static_assert(ObjectsPerPacket > 0,
                      "T does not fit within MTU - needs mbuf chaining, not "
                      "supported here");

        static constexpr size_t MaxPackets =
            BatchSize / ObjectsPerPacket + (BatchSize % ObjectsPerPacket != 0);

        using TQueue = moodycamel::BlockingReaderWriterCircularBuffer<T *>;
        using PacketsBuffer =
            std::conditional_t<(BatchSize > 0),
                               std::array<rte_mbuf *, MaxPackets>,
                               std::monostate>;

    protected:
        [[no_unique_address]] PacketsBuffer Packets;
        TQueue &Outgoing;
        StoragePool<T> &TPool;
        uint16_t PortId;
        uint16_t QueueId;
        unsigned LcoreId;
        rte_mempool *Mempool;
        SequenceRingBuffer<T, RingBufferSize> ReceiveBuffer;
        uint32_t NextToRead;
        uint32_t DstIp;
        uint16_t DstPort;

    public:
        UdpMulticastReceiver(TQueue &outgoing, StoragePool<T> &pool,
                             uint16_t portId, uint16_t nbRxQueueSlots,
                             uint16_t queueId, unsigned lcoreId,
                             const char *poolName, size_t poolSize,
                             uint32_t dstIp, uint32_t dstPort)
            : Outgoing(outgoing)
            , TPool(pool)
            , PortId(portId)
            , QueueId(queueId)
            , LcoreId(lcoreId)
            , Mempool(rte_pktmbuf_pool_create(
                  poolName, poolSize,
                  std::min<size_t>(poolSize, RTE_MEMPOOL_CACHE_MAX_SIZE), 0,
                  RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(lcoreId)))
            , NextToRead(0)
            , DstIp(dstIp)
            , DstPort(dstPort)
        {
            int ret = rte_eth_rx_queue_setup(PortId, QueueId, nbRxQueueSlots,
                                             rte_lcore_to_socket_id(lcoreId),
                                             nullptr, Mempool);

            if (ret < 0)
            {
                rte_exit(EXIT_FAILURE, "Failed setting up Rx queue\n");
            }

            rte_ether_addr mcastMac;
            ipv4_multicast_to_mac(DstIp, &mcastMac);

            ret = rte_eth_dev_mac_addr_add(PortId, &mcastMac, 0);

            if (ret != 0)
            {
                std::cerr << "Explicit multicast MAC filter unsupported (err "
                          << ret << "), trying allmulticast\n";
                ret = rte_eth_allmulticast_enable(PortId);
                if (ret != 0)
                {
                    std::cerr << "Allmulticast unsupported (err " << ret
                              << "), falling back to promiscuous mode\n";
                    ret = rte_eth_promiscuous_enable(PortId);
                    if (ret != 0)
                    {
                        rte_exit(EXIT_FAILURE,
                                 "Failed to enable promiscuous mode: %d\n",
                                 ret);
                    }
                }
            }
        }

        void Receive(void) noexcept
        {
            uint16_t nbRx =
                rte_eth_rx_burst(PortId, QueueId, Packets.data(), MaxPackets);

            for (uint16_t i = 0; i < nbRx; ++i)
            {
                rte_mbuf *pkt = Packets[i];

                std::cout << "Processing a market update packet\n\n";

                uint32_t dataLen = rte_pktmbuf_pkt_len(pkt);

                if (pkt->nb_segs > 1
                    || (pkt->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_MASK)
                        == RTE_MBUF_F_RX_IP_CKSUM_BAD
                    || (pkt->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_MASK)
                        == RTE_MBUF_F_RX_L4_CKSUM_BAD)
                {
                    std::cout << "Something wrong with checksum or number of "
                                 "packet per mbuf\n\n";
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                constexpr size_t headerSize = sizeof(rte_ether_hdr)
                    + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr);

                if (dataLen < headerSize)
                {
                    std::cout << "Data shorter than headers\n\n";
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                uint8_t *data = rte_pktmbuf_mtod(pkt, uint8_t *);

                rte_ether_hdr *eth = (rte_ether_hdr *)data;

                if (eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
                {
                    std::cout << "Not ipv4\n\n";
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                rte_ipv4_hdr *ip =
                    (rte_ipv4_hdr *)(data + sizeof(rte_ether_hdr));

                if (ip->next_proto_id != IPPROTO_UDP)
                {
                    std::cout << "Not UDP, proto=" << (int)ip->next_proto_id
                              << "\n\n";
                    rte_pktmbuf_free(pkt);
                    continue;
                }
                if (ip->dst_addr != rte_cpu_to_be_32(DstIp))
                {
                    std::cout << "Wrong dst ip: got " << std::hex
                              << rte_be_to_cpu_32(ip->dst_addr) << " expected "
                              << DstIp << std::dec << "\n\n";
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                rte_udp_hdr *udp =
                    (rte_udp_hdr *)((uint8_t *)ip + sizeof(rte_ipv4_hdr));

                if (udp->dst_port != rte_cpu_to_be_16(DstPort))
                {
                    std::cout << "Wrong dst port\n\n";
                    continue;
                }

                uint8_t *payload = (uint8_t *)udp + sizeof(rte_udp_hdr);

                size_t payloadSize = dataLen - headerSize;

                for (size_t consumed = 0; consumed < payloadSize;
                     consumed += sizeof(T))
                {
                    T *curObj = TPool.acquire();

                    if (!curObj) [[unlikely]]
                    {
                        std::cerr << "Missed an obj in a packet because "
                                     "mempool is full\n";
                        continue;
                    }

                    std::memcpy(curObj, payload + consumed, sizeof(T));

                    bool added = ReceiveBuffer.AddSlot(curObj);

                    if (added)
                    {
                        std::cout << "Added market update to ring buffer\n\n";
                        // FIXME: Forgot what I wanted to do here
                    }
                    else
                    {
                        // TODO: Free without spsc (save pointer locally)
                    }
                }

                T *readSlot = nullptr;

                while ((readSlot = ReceiveBuffer.GetSlot(NextToRead)))
                {
                    bool pushed = Outgoing.try_enqueue(readSlot);

                    if (!pushed) [[unlikely]]
                    {
                        // TODO: Free without spsc (save pointer locally)
                    }
                    else
                    {
                        std::cout << "pushing a market update to client states "
                                     "writer\n\n";
                    }

                    ++NextToRead;
                }

                std::cout << "Drained ring buffer\n\n";

                rte_pktmbuf_free(pkt);
            }
        }

        void StartLoop() noexcept
        {
            while (true)
            {
                Receive();
            }
        }
    };
    // Should be moved to a "dpdk utils" file or something like that
    static void ipv4_multicast_to_mac(uint32_t ip_host_order,
                                      struct rte_ether_addr *mac)
    {
        mac->addr_bytes[0] = 0x01;
        mac->addr_bytes[1] = 0x00;
        mac->addr_bytes[2] = 0x5E;
        mac->addr_bytes[3] = (ip_host_order >> 16) & 0x7F;
        mac->addr_bytes[4] = (ip_host_order >> 8) & 0xFF;
        mac->addr_bytes[5] = ip_host_order & 0xFF;
    }
} // namespace AccountService