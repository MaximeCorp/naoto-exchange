#pragma once

#include <ClientStates.hpp>
#include <Consumer.hpp>
#include <OrderStateReport.hpp>
#include <array>

namespace ClientStatesProvider
{
    template <size_t MaxPositions, size_t BatchSize>
    class ClientStatesWriter
        : public Consumer<ClientStatesWriter, OrderStateReport, BatchSize>
    {
    private:
        ClientStates<MaxPositions> &States;
        absl::flat_hash_set<uint32_t> Touched;

    public:
        ClientStatesWriter()
        {
            Touched.reserve(BatchSize);
        }

        void Handle(OrderStateReport *report) noexcept
        {
            States.SetClientAssets(report->ClientId, report->BoughtDelta, 0,
                                   report->BoughtAssetId);
            States.SetClientAssets(report->ClientId, report->SoldDelta,
                                   report->SoldDelta, report->SoldAsset);
            States.FlushTripleBuffer(report->ClientId);
        }

        void Handle(std::array<OrderStateReport *, BatchSize> &reportBatch,
                    size_t batchSize) noexcept
        {
            for (size_t i = 0; i < batchSize; ++i)
            {
                OrderStateReport *report = reportBatch[i];

                States.SetClientAssets(report->ClientId, report->BoughtDelta, 0,
                                       report->BoughtAssetId);
                States.SetClientAssets(report->ClientId, report->SoldDelta,
                                       report->SoldDelta, report->SoldAssetId);
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
    };
} // namespace ClientStatesProvider