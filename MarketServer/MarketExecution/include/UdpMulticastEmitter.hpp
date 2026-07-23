#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <variant>

namespace MarketExecution
{

    static void ipv4_multicast_to_mac(uint32_t ip_host_order,
                                      rte_ether_addr *mac);

    template <typename T>
    concept HasSequenceId = requires(T t) {
        { t.SequenceId } -> std::same_as<uint32_t>;
    };

    template <typename DeriverEmitter, typename T, size_t MTU = 1500,
              size_t BatchSize = 0>
        requires HasSequenceId<T>
    class UdpMulticastEmitter
    {
        static constexpr size_t ObjectsPerPacket =
            (MTU - sizeof(rte_ipv4_hdr) - sizeof(rte_udp_hdr)) / sizeof(T);

        static_assert(ObjectsPerPacket > 0,
                      "T does not fit within MTU - needs mbuf chaining, not "
                      "supported here");

        static constexpr size_t MaxPackets =
            BatchSize / ObjectsPerPacket + (BatchSize % ObjectsPerPacket != 0);

        using PacketsBuffer =
            std::conditional_t<(BatchSize > 0),
                               std::array<rte_mbuf *, MaxPackets>,
                               std::monostate>;

    protected:
        [[no_unique_address]] PacketsBuffer Packets;
        uint32_t SequenceId;
        const uint16_t PortId;
        const uint16_t QueueId;
        const unsigned LcoreId;
        rte_mempool *Mempool;
        const uint32_t SrcIp;
        const uint32_t DstIp;
        const uint16_t SrcPort;
        const uint16_t DstPort;
        rte_ether_addr DstMac;
        rte_ether_addr SrcMac;

        // Should go to utils functions
        [[nodiscard]] bool FillHeaders(rte_mbuf *const pkt,
                                       const size_t curStep,
                                       const T *const *const items) noexcept
        {
            const size_t totalLen = sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr)
                + sizeof(rte_udp_hdr) + sizeof(T) * curStep;
            char *buf = rte_pktmbuf_append(pkt, totalLen);
            if (!buf)
            {
                std::cerr << "Failed to reserve mbuf space for packet\n";
                return false;
            }

            auto *eth = reinterpret_cast<rte_ether_hdr *>(buf);
            auto *ip =
                reinterpret_cast<rte_ipv4_hdr *>(buf + sizeof(rte_ether_hdr));
            auto *udp = reinterpret_cast<rte_udp_hdr *>(
                reinterpret_cast<char *>(ip) + sizeof(rte_ipv4_hdr));
            auto *payload =
                reinterpret_cast<uint8_t *>(udp) + sizeof(rte_udp_hdr);

            FillEthernetHeader(eth);
            FillIpHeader(ip, curStep);
            FillUdpHeader(udp, curStep);
            CopyPayload(payload, items, curStep);

            pkt->l2_len = sizeof(rte_ether_hdr);
            pkt->l3_len = sizeof(rte_ipv4_hdr);
            return true;
        }

        void FillEthernetHeader(rte_ether_hdr *eth) const noexcept
        {
            rte_ether_addr_copy(&SrcMac, &eth->src_addr);
            rte_ether_addr_copy(&DstMac, &eth->dst_addr);
            eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
        }

        void FillIpHeader(rte_ipv4_hdr *ip, size_t curStep) const noexcept
        {
            ip->version_ihl = 0x45;
            ip->type_of_service = 0;
            ip->total_length =
                rte_cpu_to_be_16(sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr)
                                 + sizeof(T) * curStep);
            ip->packet_id = 0;
            ip->fragment_offset = 0;
            ip->time_to_live = 1;
            ip->next_proto_id = IPPROTO_UDP;
            ip->src_addr = rte_cpu_to_be_32(SrcIp);
            ip->dst_addr = rte_cpu_to_be_32(DstIp);
            ip->hdr_checksum = 0;
            ip->hdr_checksum = rte_ipv4_cksum(ip);
        }

        void FillUdpHeader(rte_udp_hdr *udp, size_t curStep) const noexcept
        {
            udp->src_port = rte_cpu_to_be_16(SrcPort);
            udp->dst_port = rte_cpu_to_be_16(DstPort);
            udp->dgram_len =
                rte_cpu_to_be_16(sizeof(rte_udp_hdr) + sizeof(T) * curStep);
        }

        void CopyPayload(uint8_t *payload, const T *const *items,
                         size_t curStep) const noexcept
        {
            if constexpr (BatchSize == 0)
            {
                memcpy(payload, *items, sizeof(T));
            }
            else
            {
                for (size_t i = 0; i < curStep; ++i)
                {
                    memcpy(payload + i * sizeof(T), items[i], sizeof(T));
                }
            }
        }

    public:
        UdpMulticastEmitter(const uint16_t portId,
                            const uint16_t nbTxQueueSlots,
                            const uint16_t queueId, const unsigned lcoreId,
                            const char *poolName, const size_t poolSize,
                            const uint32_t srcIp, const uint32_t dstIp,
                            uint16_t srcPort, uint16_t dstPort)

            : SequenceId(0)
            , PortId(portId)
            , QueueId(queueId)
            , LcoreId(lcoreId)
            , Mempool(rte_pktmbuf_pool_create(
                  poolName, poolSize,
                  std::min<size_t>(poolSize, RTE_MEMPOOL_CACHE_MAX_SIZE), 0,
                  RTE_PKTMBUF_HEADROOM + MTU + sizeof(rte_ether_hdr),
                  rte_lcore_to_socket_id(lcoreId)))
            , SrcIp(srcIp)
            , DstIp(dstIp)
            , SrcPort(srcPort)
            , DstPort(dstPort)
        {
            int ret = rte_eth_tx_queue_setup(portId, queueId, nbTxQueueSlots,
                                             rte_lcore_to_socket_id(lcoreId),
                                             nullptr);

            if (ret < 0)
            {
                rte_exit(EXIT_FAILURE, "Failed setting up Tx queue\n");
            }

            ret = rte_eth_macaddr_get(PortId, &SrcMac);

            if (ret != 0)
            {
                rte_exit(EXIT_FAILURE, "Failed to get MAC address: %d\n", ret);
            }

            ipv4_multicast_to_mac(DstIp, &DstMac);
        }

        void Send(T *const object) noexcept
        {
            object->SequenceId = SequenceId++;

            rte_mbuf *pkt = rte_pktmbuf_alloc(Mempool);

            if (!pkt) [[unlikely]]
            {
                std::cerr << "Failed acquiring packet from dpdk mempool\n";
                return;
            }

            if (!FillHeaders(pkt, 1, &object)) [[unlikely]]
            {
                rte_pktmbuf_free(pkt);
                return;
            }

            uint16_t sent = rte_eth_tx_burst(PortId, QueueId, &pkt, 1);

            if (sent == 0) [[unlikely]]
            {
                rte_pktmbuf_free(pkt);
            }
        }

        void Send(const std::array<T *const, BatchSize> &buffer,
                  size_t curSize) noexcept
            requires(BatchSize > 0)
        {
            size_t curPacket = 0;
            size_t totalCopied = 0;

            while (totalCopied < curSize && curPacket < MaxPackets)
            {
                size_t curStep =
                    std::min(ObjectsPerPacket, curSize - totalCopied);

                for (size_t i = 0; i < curStep; ++i)
                {
                    buffer[totalCopied + i]->SequenceId = SequenceId++;
                }

                Packets[curPacket] = rte_pktmbuf_alloc(Mempool);

                if (!Packets[curPacket]) [[unlikely]]
                {
                    std::cerr << "Failed acquiring packet from dpdk mempool\n";
                    totalCopied += curStep;
                    continue;
                }

                bool built = FillHeaders(Packets[curPacket], curStep,
                                         &buffer[totalCopied]);

                totalCopied += curStep;
                curPacket += built;

                if (!built) [[unlikely]]
                {
                    rte_pktmbuf_free(Packets[curPacket]);
                    Packets[curPacket] = nullptr;
                }
            }

            if (curPacket == 0) [[unlikely]]
            {
                return;
            }

            uint16_t sent =
                rte_eth_tx_burst(PortId, QueueId, &Packets[0], curPacket);

            if (sent < curPacket)
            {
                rte_pktmbuf_free_bulk(&Packets[sent], curPacket - sent);
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
} // namespace MarketExecution
