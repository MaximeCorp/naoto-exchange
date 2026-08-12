#pragma once

#include <ClientRequestResponse.hpp>
#include <ClientStates.hpp>
#include <Consumer.hpp>
#include <FlatHashMap.hpp>
#include <OrderStateReport.hpp>
#include <absl/container/flat_hash_set.h>
#include <array>

namespace Gateways
{
    template <size_t MaxClients, size_t MaxPositions, size_t BatchSize,
              size_t BufferSize>
    class ClientStatesWriter
        : public Consumer<ClientStatesWriter<MaxPositions, BatchSize>,
                          OrderStateReport, BatchSize>
        , public Consumer<
              ClientStatesWriter<MaxPositions, BatchSize>,
              ObjectBatch<ClientRequestResponse<MaxPositions>, BatchSize>>

    {
        using ReportBase = Consumer<ClientStatesWriter<MaxPositions, BatchSize>,
                                    OrderStateReport, BatchSize>;
        using ReportQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<OrderStateReport *>;

        using ResponseBatch =
            ObjectBatch<ClientRequestResponse<MaxPositions>, BatchSize>;
        using ResponseBase =
            Consumer<ClientStatesWriter<MaxPositions, BatchSize>, ResponseBatch,
                     BatchSize>,
              BatchSize > ;
        using ResponseQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<ResponseBatch *>;

    private:
        ClientStates<MaxPositions> &States;
        FlatHashMap<uint32_t, uint32_t, MaxClients> ClientsFd;
        absl::flat_hash_set<uint32_t> Touched;
        std::array<OrderStateReport, BufferSize> UpdatesBuffer;
        uint64_t LastSeq;

    public:
        ClientStatesWriter(ClientStates<MaxPositions> &states,
                           ReportQueue &incomingReports,
                           StoragePool<OrderStateReport> &reportPool,
                           ResponseQueue &incomingResponses,
                           StoragePool<ResponseBatch> &responsePool)
            : ReportBase(incomingReports reportPool)
            , ResponseBase(incomingResponses, responsePool)
            , States(states)
            , LastSeq(0)
        {
            Touched.reserve(BatchSize);

            // Fill with invalid SequenceId to mark as empty
            for (size_t i = 0; i < BufferSize; ++i)
            {
                UpdatesBuffer[i].SequenceId = i + 1;
            }
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

            LastSeq = report->SequenceId;

            UpdatesBuffer[report->SequenceId & (BufferSize - 1)] = *report;

            uint32_t clientFd;

            bool found = ClientsFd.GetVal(report->ClientId, clientFd);

            if (!found)
            {
                return;
            }

            States.SetClientAssets(clientFd, report->BoughtDelta, 0,
                                   report->BoughtAssetId);
            States.SetClientAssets(clientFd, report->SoldDelta,
                                   report->SoldDelta, report->SoldAssetId);
            States.FlushTripleBuffer(clientFd);
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

                LastSeq = report->SequenceId;

                UpdatesBuffer[report->SequenceId & (BufferSize - 1)] = *report;

                uint32_t clientFd;

                bool found = ClientsFd.GetVal(report->ClientId, clientFd);

                if (!found)
                {
                    continue;
                }

                States.SetClientAssets(clientFd, report->BoughtDelta, 0,
                                       report->BoughtAssetId);
                States.SetClientAssets(clientFd, report->SoldDelta,
                                       report->SoldDelta, report->SoldAssetId);
            }

            for (size_t i = 0; i < batchSize; ++i)
            {
                uint32_t clientFd;

                bool found =
                    ClientsFd.GetVal(reportBatch[i]->ClientId, clientFd);

                if (!found || Touched.contains(clientFd))
                {
                    continue;
                }

                States.FlushTripleBuffer(clientFd);

                Touched.insert(clientFd);
            }

            Touched.clear();
        }

        void Handle(ClientRequestResponse *response) noexcept
        {
            if (UpdatesBuffer[response->SequenceId & (BufferSize - 1)]
                != response->SequenceId) [[unlikely]]
            {
                // Resend the request
                std::cerr << "Response sequence id is stale.\n\n";
                return;
            }

            // Iterate buffer to apply missed delta
            uint64_t seqId = response->SequenceId + 1;

            while (seqId <= LastSeq)
            {
                OrderStateReport &curReport =
                    UpdatesBuffer[seqId++ & (BufferSize - 1)];

                if (curReport.ClientId == response->ClientId)
                {
                    for (size_t i = 0; i < MaxPositions; ++i)
                    {
                        if (response->AssetId[i] == curReport.BoughtAssetId)
                        {
                            response->Confirmed[i] += curReport.BoughtDelta;
                        }
                        else if (response->AssetId[i] == curReport.SoldAssetId)
                        {
                            response->Confirmed += curReport.SoldDelta;
                            response->Attempt[i] += curReport.SoldDelta;
                        }
                    }
                }
            }

            States.SetClientState(response);
            States.FlushTripleBuffer(response->ClientFd);
        }

        void Handle(std::array<responseBatch *, BatchSize> &responseBatch,
                    size_t batchSize) noexcept
        {
            for (size_t i = 0; i < batchSize; ++i)
            {
                ResponseBatch *response = responseBatch[i];

                for (size_t j = 0; j < response->Size; ++j)
                {
                    if (UpdatesBuffer[response->SequenceId & (BufferSize - 1)]
                        != response->SequenceId) [[unlikely]]
                    {
                        // Resend the request
                        std::cerr << "Response sequence id is stale.\n\n";
                        continue;
                    }

                    // Iterate buffer to apply missed delta
                    uint64_t seqId = response->SequenceId + 1;

                    while (seqId <= LastSeq)
                    {
                        OrderStateReport &curReport =
                            UpdatesBuffer[seqId++ & (BufferSize - 1)];

                        if (curReport.ClientId == response->ClientId)
                        {
                            for (size_t k = 0; k < MaxPositions; ++k)
                            {
                                if (response->AssetId[k]
                                    == curReport.BoughtAssetId)
                                {
                                    response->Confirmed[k] +=
                                        curReport.BoughtDelta;
                                }
                                else if (response->AssetId[k]
                                         == curReport.SoldAssetId)
                                {
                                    response->Confirmed += curReport.SoldDelta;
                                    response->Attempt[k] += curReport.SoldDelta;
                                }
                            }
                        }
                    }

                    States.SetClientState(response);
                }
            }

            for (size_t i = 0; i < batchSize; ++i)
            {
                uint32_t clientFd = responseBatch[i]->ClientFd;

                if (Touched.contains(clientFd))
                {
                    continue;
                }

                States.FlushTripleBuffer(clientFd);

                Touched.insert(clientFd);
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
} // namespace Gateways