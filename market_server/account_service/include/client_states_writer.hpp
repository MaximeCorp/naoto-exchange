#pragma once

#include <absl/container/flat_hash_set.h>
#include <array>
#include <client_states.hpp>
#include <consumer.hpp>
#include <order_state_report.hpp>
#include <spsc_queue.hpp>
#include <system_conf.hpp>

namespace naoto::account_service
{
    class ClientStatesWriter
        : public Consumer<
              ClientStatesWriter, OrderStateReport, TradeReportReceiveQueueSize,
              TradeReportReceivePoolSize, TradeReportReceiveBatchSize>
    {
        using Base =
            Consumer<ClientStatesWriter, OrderStateReport,
                     TradeReportReceiveQueueSize, TradeReportReceivePoolSize,
                     TradeReportReceiveBatchSize>;
        using ReportQueue = SpscQueue<OrderStateReport *, MaxClients>;

    private:
        ClientStates<MaxPositions> &States;
        absl::flat_hash_set<uint32_t> Touched;

    public:
        ClientStatesWriter(
            ClientStates<MaxPositions> &states, ReportQueue *incoming,
            StoragePool<OrderStateReport, TradeReportReceivePoolSize> &mempool)
            : Base(incoming, mempool)
            , States(states)
        {
            Touched.reserve(TradeReportReceiveBatchSize);
        }

        void Handle(OrderStateReport *report) noexcept
        {
            std::cout << "Received market update\n";

            std::cout << "Client Id: " << report->ClientId << "\n";
            std::cout << "Bought Asset Id: " << report->BoughtAssetId << "\n";
            std::cout << "Bought Asset Delta: " << report->BoughtDelta << "\n";
            std::cout << "Sold Asset Id: " << report->SoldAssetId << "\n";
            std::cout << "Sold Asset Delta: " << report->SoldDelta << "\n";
            std::cout << "Sequence Id: " << report->SequenceId << "\n";

            States.SetClientAssets(report->ClientId, report->BoughtDelta, 0,
                                   report->BoughtAssetId, report->SequenceId);
            States.SetClientAssets(report->ClientId, report->SoldDelta,
                                   report->SoldAttemptDelta,
                                   report->SoldAssetId, report->SequenceId);
            States.FlushTripleBuffer(report->ClientId);
        }

        void Handle(std::array<OrderStateReport *, TradeReportReceiveBatchSize>
                        &reportBatch,
                    size_t batchSize) noexcept
        {
            std::cout << "Received market update batch of size " << batchSize
                      << "\n";

            for (size_t i = 0; i < batchSize; ++i)
            {
                OrderStateReport *report = reportBatch[i];

                std::cout << "Client Id: " << report->ClientId << "\n";
                std::cout << "Bought Asset Id: " << report->BoughtAssetId
                          << "\n";
                std::cout << "Bought Asset Delta: " << report->BoughtDelta
                          << "\n";
                std::cout << "Sold Asset Id: " << report->SoldAssetId << "\n";
                std::cout << "Sold Asset Delta: " << report->SoldDelta << "\n";
                std::cout << "Sequence Id: " << report->SequenceId << "\n";

                States.SetClientAssets(report->ClientId, report->BoughtDelta, 0,
                                       report->BoughtAssetId,
                                       report->SequenceId);
                States.SetClientAssets(report->ClientId, report->SoldDelta,
                                       report->SoldAttemptDelta,
                                       report->SoldAssetId, report->SequenceId);
            }

            for (size_t i = 0; i < batchSize; ++i)
            {
                uint32_t clientId = reportBatch[i]->ClientId;

                if (Touched.contains(clientId))
                {
                    continue;
                }

                States.FlushTripleBuffer(clientId);

                Touched.insert(clientId);
            }

            Touched.clear();
        }

        void StartLoop(void) noexcept
        {
            while (true)
            {
                Base::TryConsume();
            }
        }
    };
} // namespace naoto::account_service