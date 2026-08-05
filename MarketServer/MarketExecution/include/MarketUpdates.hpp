#pragma once

#include <OrderBookEmitter.hpp>
#include <OrderBookUpdate.hpp>
#include <OrderStateReport.hpp>
#include <OrderStatusEmitter.hpp>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>

#define RX_QUEUES 0
#define TX_QUEUES 2
#define EMITTERS 2

namespace MarketExecution
{
    template <size_t BatchSize = 0>
    class MarketUpdates
    {
        using OrdersQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;
        using BookQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderBookUpdate *>;

    private:
        std::optional<OrderBookEmitter<BatchSize>> BookEmitter;
        std::optional<OrderStatusEmitter<BatchSize>> StatusEmitter;

    public:
        // Must be called on a dedicated thread
        MarketUpdates(int argc, char **argv, OrdersQueue &incomingOrderStates,
                      BookQueue &incomingBookUpdates,
                      StoragePool<OrderStateReport> &orderStatesPool,
                      StoragePool<OrderBookUpdate> &orderBookUpdatesPool,
                      const uint16_t portId, const uint16_t nbTxQueueSlots,
                      const size_t poolSize, const uint32_t srcIp,
                      const uint16_t srcPort, const uint32_t dstOrderIp,
                      const uint16_t dstOrderPort, const uint32_t dstBookIp,
                      const uint16_t dstBookPort)
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

            int assignedQueues = 0;
            bool mainLcoreComing = true;

            size_t lcoreId;
            RTE_LCORE_FOREACH(lcoreId)
            {
                if (lcoreId == mainLcore)
                {
                    BookEmitter.emplace(
                        incomingBookUpdates, orderBookUpdatesPool, portId,
                        nbTxQueueSlots, assignedQueues, lcoreId, "book_pool",
                        poolSize, srcIp, dstBookIp, srcPort, dstBookPort);
                    mainLcoreComing = false;
                }
                else if (assignedQueues + mainLcoreComing == 1)
                {
                    std::cout << "Constructing order state emitter\n\n";
                    StatusEmitter.emplace(
                        incomingOrderStates, orderStatesPool, portId,
                        nbTxQueueSlots, assignedQueues, lcoreId, "status_pool",
                        poolSize, srcIp, dstOrderIp, srcPort, dstOrderPort);
                }
                else
                {
                    continue;
                }

                ++assignedQueues;
            }

            if (!BookEmitter || !StatusEmitter)
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

        void StartEmittersLoop(void) noexcept
        {
            rte_eal_remote_launch(StartOrderStatusLoop<BatchSize>,
                                  &StatusEmitter.value(),
                                  StatusEmitter->GetLcoreId());

            BookEmitter->StartLoop();

            rte_eal_mp_wait_lcore();

            rte_eal_cleanup();
        }
    };
} // namespace MarketExecution