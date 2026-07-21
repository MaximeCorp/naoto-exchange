#pragma once

#include <OrderBookEmitter.hpp>
#include <OrderStatusEmitter.hpp>
#include <cstddef>
#include <iostream>
#include <rte_eal.h>
#include <vector>

namespace MarketExecution
{
    class MarketUpdates
    {
    private:
        OrderBookEmitter BookEmitter;
        OrderStatusEmitter StatusEmitter;

    public:
        // Should be called with rte_eal_remote_launch
        void StartEmittersLoop(int argc, char **argv) noexcept
        {
            int ret = rte_eal_init(argc, argv);

            if (ret < 0)
            {
                std::cerr << "rte_eal_init failed\n";
                std::terminate();
            }

            size_t mainLcore = rte_get_main_lcore();

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
                }
            }

            BookEmitter.StartLoop();

            ret = rte_eal_mp_wait_lcore();
        }
    };
} // namespace MarketExecution