#pragma once

#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <order_state_report.hpp>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <udp_multicast_receiver.hpp>

#define RX_QUEUES 1
#define TX_QUEUES 0
#define EMITTERS 0
#define RECEIVERS 1

namespace naoto::order_gateway
{
    template <size_t RingBufferSize, size_t BatchSize = 0>
    class MarketUpdates
    {
        using ReportQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;

    private:
        std::optional<
            UdpMulticastReceiver<OrderStateReport, RingBufferSize, BatchSize>>
            ReportReceiver;

    public:
        // Must be called on a dedicated thread
        MarketUpdates(int argc, char **argv, ReportQueue &incomingOrderStates,
                      StoragePool<OrderStateReport> &orderStatesPool,
                      const uint16_t portId, const uint16_t nbRxQueueSlots,
                      const size_t poolSize, const uint32_t dstIp,
                      const uint16_t dstPort)
        {
            int ret = rte_eal_init(argc, argv);

            if (ret < 0)
            {
                std::cerr << "rte_eal_init failed\n";
                std::terminate();
            }

            const size_t mainLcore = rte_get_main_lcore();

            rte_eth_conf portConf;
            std::memset(&portConf, 0, sizeof(portConf));

            ret =
                rte_eth_dev_configure(portId, RX_QUEUES, TX_QUEUES, &portConf);

            if (ret < 0)
            {
                rte_exit(EXIT_FAILURE, "Port configure failed: %d\n", ret);
            }

            ReportReceiver.emplace(incomingOrderStates, orderStatesPool, portId,
                                   nbRxQueueSlots, 0, mainLcore, "report_pool",
                                   poolSize, dstIp, dstPort);

            if (!ReportReceiver)
            {
                rte_exit(EXIT_FAILURE,
                         "Not enough lcores to assign emitters\n");
            }

            ret = rte_eth_dev_start(portId);

            if (ret < 0)
            {
                rte_exit(EXIT_FAILURE, "Device start failed: %d\n", ret);
            }
        }

        void StartReceiversLoop(void) noexcept
        {
            ReportReceiver->StartLoop();

            rte_eal_mp_wait_lcore();

            rte_eal_cleanup();
        }
    };
} // namespace naoto::order_gateway