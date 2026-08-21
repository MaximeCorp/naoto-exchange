#pragma once

#include <ClientStates.hpp>
#include <Consumer.hpp>
#include <OrderStateReport.hpp>
#include <absl/container/flat_hash_set.h>
#include <array>

namespace AccountService
{
    template <size_t MaxPositions, size_t BatchSize>
    class ClientStatesWriter
        : public Consumer<ClientStatesWriter<MaxPositions, BatchSize>,
                          OrderStateReport, BatchSize>
    {
        using Base = Consumer<ClientStatesWriter<MaxPositions, BatchSize>,
                              OrderStateReport, BatchSize>;
        using ReportQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;

    private:
        ClientStates<MaxPositions> &States;
        absl::flat_hash_set<uint32_t> Touched;

    public:
        ClientStatesWriter(ClientStates<MaxPositions> &states,
                           ReportQueue &incoming,
                           StoragePool<OrderStateReport> &mempool)
            : Base(incoming, mempool)
            , States(states)
        {
            Touched.reserve(BatchSize);
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
                                   report->SoldDelta, report->SoldAssetId,
                                   report->SequenceId);
            States.FlushTripleBuffer(report->ClientId);
        }

        void Handle(std::array<OrderStateReport *, BatchSize> &reportBatch,
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
                                       report->SoldDelta, report->SoldAssetId,
                                       report->SequenceId);
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
} // namespace AccountService