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
            clientStates; // Only for read (another object will write in it)
        std::vector<std::uint32_t>
            MatchingEngines; // Mapping from asset ID to matching engine fd

        StoragePool<OrderBatch<BatchSize>> &OrdersPool;
        OrderQueue &Orders;

        inline void consumeOrder(void) noexcept
        {
            OrderBatch<BatchSize> *to_check = nullptr;

            if (Orders.try_dequeue(to_check)) [[likely]]
            {
                for (size_t i = 0; i < to_check->getSize(); ++i)
                {
                    const Order &cur_order = (*to_check)[i];
                    if (clientStates.can_spend(cur_order.getClientId(),
                                               cur_order.getAmount()))
                        [[likely]]
                    {
                        // send order to MatchingEngines[to_check->asset_ID]
                    }
                }
            }
            else
            {
                // Write error message to client fd
            }
        }

        void connectMatchingEngines(void) // Might need some args
        {
            // Todo
        }

    public:
        RiskService(StoragePool<OrderBatch<BatchSize>> &ordersPool,
                    OrderQueue &orders, size_t nb_fds)
            : clientStates(ClientStates(nb_fds))
            , OrdersPool(ordersPool)
            , Orders(orders)
        {
            connectMatchingEngines();
        }
        RiskService(StoragePool<Order> &ordersPool, OrderQueue &orders)
            : clientStates(make_fd_array())
            , OrdersPool(ordersPool)
            , Orders(orders)
        {
            connectMatchingEngines();
        }

        void startLoop(void)
        {
            while (true) // Maybe add ability to stop the loop
            {
                consumeOrder();
            }
        }
    };
} // namespace Gateways
