#pragma once

#include <ClientStates.hpp>
#include <Order.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <cstdint>
#include <vector>

namespace Gateways
{
    template <size_t BatchSize>
    class RiskService
    {
        using OrderQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            OrderBatch<BatchSize> *>;

    private:
        ClientStates
            &clientStates; // Only for read (another object will write in it)

        StoragePool<OrderBatch<BatchSize>> &OrdersPool;
        OrderQueue &Orders;

        inline void consumeOrder(void) noexcept
        {
            OrderBatch<BatchSize> *to_check = nullptr;

            if (Orders.try_dequeue(to_check)) [[likely]]
            {
                std::cout << "Received order batch of size "
                          << to_check->getSize() << " at risk service\n";

                for (size_t i = 0; i < to_check->getSize(); ++i)
                {
                    const Order &cur_order = (*to_check)[i];

                    cur_order.log();

                    if (clientStates.can_spend(to_check->getFd(),
                                               cur_order.getAmount()))
                        [[likely]]
                    {
                        // send order to MatchingEngines[to_check->asset_ID]
                    }
                }

                if (!OrdersPool.release(to_check))
                {
                    perror("Failed mempool release.\n");
                }
            }
            else
            {
                // Write error message to client fd
            }
        }

    public:
        RiskService(StoragePool<OrderBatch<BatchSize>> &ordersPool,
                    OrderQueue &orders, ClientStates &clientStates)
            : clientStates(clientStates)
            , OrdersPool(ordersPool)
            , Orders(orders)
        {}

        void startLoop(void)
        {
            while (true) // Maybe add ability to stop the loop
            {
                consumeOrder();
            }
        }
    };
} // namespace Gateways
