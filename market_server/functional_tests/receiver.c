#include <netinet/in.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PORT_ID 0
#define MBUF_POOL_SIZE 1024
#define MBUF_CACHE_SIZE 256
#define BURST_SIZE 32
#define TARGET_UDP_PORT 30002 /* matches OrderBookUpdate / dstBookPort */
#define STATUS_INTERVAL 5000

#pragma pack(push, 1)
struct OrderBookUpdate
{
    uint32_t SequenceId;
    uint32_t Depth;
    int64_t Price;
    uint16_t AssetId;
    uint8_t Side;
};
#pragma pack(pop)

static volatile int running = 1;
static uint64_t irrelevantCount = 0;

/* sequence tracking state */
static int haveSeen =
    0; /* have we received at least one matching packet yet? */
static uint32_t lastSeq = 0; /* last SequenceId we accepted as "next in line" */
static uint64_t totalReceived = 0;
static uint64_t totalGapPackets =
    0; /* sum of missing seq numbers across all gaps */

static void handleSigint(int sig)
{
    (void)sig;
    running = 0;
}

static void checkSequence(uint32_t seq)
{
    totalReceived++;

    if (!haveSeen)
    {
        haveSeen = 1;
        lastSeq = seq;
        printf("  [seq] first packet, baseline seq=%u\n", seq);
        return;
    }

    uint32_t expected = lastSeq + 1;

    if (seq == expected)
    {
        lastSeq = seq;
    }
    else if (seq > expected)
    {
        uint32_t missing = seq - expected;
        totalGapPackets += missing;
        printf("  [seq] GAP: expected %u, got %u (missing %u packet(s))\n",
               expected, seq, missing);
        lastSeq = seq;
    }
    else /* seq <= lastSeq */
    {
        printf("  [seq] OUT-OF-ORDER/DUPLICATE: expected %u, got %u (seq did "
               "not advance)\n",
               expected, seq);
        /* don't move lastSeq backwards */
    }
}

int main(int argc, char **argv)
{
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
    {
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    }

    signal(SIGINT, handleSigint);

    struct rte_mempool *mbufPool =
        rte_pktmbuf_pool_create("RX_MBUF_POOL", MBUF_POOL_SIZE, MBUF_CACHE_SIZE,
                                0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbufPool == NULL)
    {
        rte_exit(EXIT_FAILURE, "Failed to create mbuf pool\n");
    }

    struct rte_eth_conf portConf;
    memset(&portConf, 0, sizeof(portConf));

    ret = rte_eth_dev_configure(PORT_ID, 1, 0, &portConf);
    if (ret < 0)
    {
        rte_exit(EXIT_FAILURE, "Port configure failed: %d\n", ret);
    }

    ret = rte_eth_rx_queue_setup(PORT_ID, 0, 256, rte_socket_id(), NULL,
                                 mbufPool);
    if (ret < 0)
    {
        rte_exit(EXIT_FAILURE, "RX queue setup failed: %d\n", ret);
    }

    ret = rte_eth_dev_start(PORT_ID);
    if (ret < 0)
    {
        rte_exit(EXIT_FAILURE, "Port start failed: %d\n", ret);
    }

    ret = rte_eth_promiscuous_enable(PORT_ID);
    if (ret != 0)
    {
        printf("Warning: failed to enable promiscuous mode: %d\n", ret);
    }

    printf("Listening on port %d for UDP dst port %d... (Ctrl+C to stop)\n",
           PORT_ID, TARGET_UDP_PORT);

    struct rte_mbuf *bufs[BURST_SIZE];

    while (running)
    {
        uint16_t nbRx = rte_eth_rx_burst(PORT_ID, 0, bufs, BURST_SIZE);

        for (uint16_t i = 0; i < nbRx; ++i)
        {
            struct rte_mbuf *pkt = bufs[i];
            uint8_t *data = rte_pktmbuf_mtod(pkt, uint8_t *);
            uint32_t dataLen = rte_pktmbuf_pkt_len(pkt);

            size_t minLen = sizeof(struct rte_ether_hdr)
                + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr)
                + sizeof(struct OrderBookUpdate);
            if (dataLen < minLen)
            {
                irrelevantCount++;
                rte_pktmbuf_free(pkt);
                continue;
            }

            struct rte_ether_hdr *eth = (struct rte_ether_hdr *)data;

            if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4)
            {
                irrelevantCount++;
                rte_pktmbuf_free(pkt);
                continue;
            }

            struct rte_ipv4_hdr *ip =
                (struct rte_ipv4_hdr *)(data + sizeof(struct rte_ether_hdr));

            if (ip->next_proto_id != IPPROTO_UDP)
            {
                irrelevantCount++;
                rte_pktmbuf_free(pkt);
                continue;
            }

            struct rte_udp_hdr *udp =
                (struct rte_udp_hdr *)((uint8_t *)ip
                                       + sizeof(struct rte_ipv4_hdr));
            if (rte_be_to_cpu_16(udp->dst_port) != TARGET_UDP_PORT)
            {
                irrelevantCount++;
                rte_pktmbuf_free(pkt);
                continue;
            }

            /* matched packet */

            if (irrelevantCount > 0)
            {
                printf("(filtered %lu irrelevant packets before this one)\n",
                       (unsigned long)irrelevantCount);
                irrelevantCount = 0;
            }

            uint8_t *payload = (uint8_t *)udp + sizeof(struct rte_udp_hdr);
            struct OrderBookUpdate update;
            memcpy(&update, payload, sizeof(update));

            printf("Received: seq=%u depth=%u price=%ld assetId=%u side=%u\n",
                   update.SequenceId, update.Depth, (long)update.Price,
                   update.AssetId, update.Side);

            checkSequence(update.SequenceId);

            rte_pktmbuf_free(pkt);
        }

        if (irrelevantCount > 0 && irrelevantCount % STATUS_INTERVAL == 0)
        {
            printf("(status: %lu irrelevant packets filtered so far, still "
                   "listening)\n",
                   (unsigned long)irrelevantCount);
        }
    }

    printf("Shutting down. Total matched packets: %lu, total missing (gap) "
           "packets: %lu\n",
           (unsigned long)totalReceived, (unsigned long)totalGapPackets);

    rte_eth_dev_stop(PORT_ID);
    rte_eal_cleanup();
    return 0;
}