#pragma once

#include <OrderBookEmitter.hpp>
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
    class MarketUpdates
    {
    private:
        OrderBookEmitter BookEmitter;
        OrderStatusEmitter StatusEmitter;

    public:
        // Must be called on a dedicated thread
        MarketUpdates(uint16_t portId, int argc, char **argv)
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
                    BookEmitter =
                        OrderBookEmitter(portId, assignedQueues, lcoreId);
                    mainLcoreFound = true;
                }
                else if (assignedQueues + mainLcoreFound == 1)
                {
                    StatusEmitter =
                        OrderStatusEmitter(portId, assignedQueues, lcoreId);
                }
                else
                {
                    continue;
                }

                ++assignedQueues;
            }

            ret = rte_eth_dev_start(portId);

            if (ret < 0)
            {
                rte_exit(EXIT_FAILURE, "Device start failed: %d\n", ret);
            }
        }

        void StartEmittersLoop(void) noexcept
        {
            const size_t mainLcore = rte_get_main_lcore();

            size_t launchedEMitters = 0;

            size_t lcoreId;
            RTE_LCORE_FOREACH_WORKER(lcoreId)
            {
                if (launchedEmitters == 0)
                {
                    rte_eal_remote_launch(StartOrderStatusLoop, &StatusEmitter,
                                          lcoreId);
                }
                else // No other emitter for now
                {
                    continue;
                }

                ++launchedEMitters;
            }

            BookEmitter.StartLoop();

            ret = rte_eal_mp_wait_lcore();

            rte_eal_cleanup();
        }
    };
} // namespace MarketExecution