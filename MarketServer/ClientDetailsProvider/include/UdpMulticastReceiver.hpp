#pragma once

#include <ReaderWriterCircularBuffer.hpp>
#include <SequenceRingBuffer.hpp>
#include <StoragePool.hpp>
#include <concepts>
#include <cstddef>
#include <string>
#include <variant>

namespace ClientDetailsProvider
{
    template <typename T>
    concept HasSequenceId = requires(T t) {
        { t.SequenceId } -> std::convertible_to<uint32_t>;
    };

    template <typename DerivedReceiver, typename T, size_t RingBufferSize,
              size_t BatchSize = 0, size_t MTU = 1500>
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
        rte_ether_addr DstMac;

    public:
        UdpMulticastReceiver(TQueue &outgoing, StoragePool<T> &pool,
                             uint16_t portId, uint16_t queueId,
                             unsigned lcoreId, char *poolName, size_t poolSize)
            : Outgoing(outgoing)
            , TPool(pool)
            , PortId(portId)
            , QueueId(queueId)
            , LcoreId(lcoreId)
            , Mempool(rte_pktmbuf_pool_create(
                  poolName, poolSize,
                  std::min<size_t>(poolSize, RTE_MEMPOOL_CACHE_MAX_SIZE), 0,
                  RTE_PKTMBUF_HEADROOM + MTU + sizeof(rte_ether_hdr),
                  rte_lcore_to_socket_id(lcoreId)))
        {}

        void Receive(void) noexcept
        {
            uint16_t nbRx =
                rte_eth_rx_burst(PortId, QueueId, Packets, MaxPackets);
            for (uint16_t i = 0; i < nbRx; ++i)
            {
                rte_mbuf *pkt = Packets[i];

                uint32_t dataLen = rte_pktmbuf_pkt_len(pkt);

                if (pkt->nb_segs > 1
                    || (pkt->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_MASK)
                        == RTE_MBUF_F_RX_IP_CKSUM_BAD
                    || (pkt->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_MASK)
                        == RTE_MBUF_F_RX_L4_CKSUM_BAD)
                {
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                constexpr size_t headerSize = sizeof(rte_ether_hdr)
                    + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr);

                if (dataLen < headerSize)
                {
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                uint8_t *data = rte_pktmbuf_mtod(pkt, uint8_t *);

                rte_ether_hdr *eth = (rte_ether_hdr *)data;

                if (eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
                {
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                rte_ipv4_hdr *ip =
                    (rte_ipv4_hdr *)(data + sizeof(rte_ether_hdr));

                if (ip->next_proto_id != IPPROTO_UDP
                    || ip->dst_addr != rte_cpu_to_be_32(DstIp))
                {
                    rte_pktmbuf_free(pkt);
                    continue;
                }

                rte_udp_hdr *udp =
                    (rte_udp_hdr *)((uint8_t *)ip + sizeof(rte_ipv4_hdr));

                if (udp->dst_port != rte_cpu_to_be_16(DstPort))
                {
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
                        // Forgot what I wanted to do here
                    }
                    else
                    {
                        // Free without spsc (save pointer locally)
                    }
                }

                T *readSlot = nullptr;

                while ((readSlot = ReceiveBuffer.GetSlot(NextToRead)))
                {
                    bool pushed = Outgoing.try_enqueue(readSlot);

                    if (!pushed) [[unlikely]]
                    {
                        // Free without spsc (save pointer locally)
                    }

                    ++NextToRead;
                }

                rte_pktmbuf_free(pkt);
            }
        }
    };
} // namespace ClientDetailsProvider