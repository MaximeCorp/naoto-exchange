#pragma once

#include <absl/container/flat_hash_set.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <client_account_snapshot.hpp>
#include <client_states.hpp>
#include <order_gateway_types.hpp>
#include <flat_hash_map.hpp>
#include <object_batch.hpp>
#include <order_state_report.hpp>
#include <routed_auth_request.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <system_conf.hpp>

namespace naoto::order_gateway
{
    class ClientStatesWriter
        : public StatesWriterReportBase
        , public StatesWriterResponseBase

    {
    private:
        const uint16_t GatewayId;
        ClientStates &States;
        ClientFdMap ClientsFd;
        absl::flat_hash_set<uint32_t> Touched;
        TradeReportUpdatesBuffer UpdatesBuffer;
        uint64_t LastSeq;
        // ASSUMPTION: these two were declared as references
        // (SpscQueueConsumer& / SpscQueue&) but initialised from queue
        // pointers. Now held by value as consumer/producer handles, exactly
        // like every other class in the project does.
        DisconnectConsumer IncomingDisconnects;
        AuthRequestMempool &GatewayReqPool;
        AuthRequestProducer OutgoingReq;

    public:
        ClientStatesWriter(ClientStates &states,
                           TradeReportQueue *incomingReports,
                           TradeReportMempool &reportPool,
                           AccountResponseQueue *incomingResponses,
                           AccountResponseMempool &responsePool,
                           DisconnectQueue *incomingDisconnects,
                           AuthRequestQueue *outgoingReq,
                           AuthRequestMempool &gatewayReqPool)
            : StatesWriterReportBase(incomingReports, reportPool)
            , StatesWriterResponseBase(incomingResponses, responsePool)
            , GatewayId(std::stoi(std::getenv("MACHINE_ID") ?: "0"))
            , States(states)
            , LastSeq(0)
            , IncomingDisconnects(incomingDisconnects)
            , GatewayReqPool(gatewayReqPool)
            , OutgoingReq(outgoingReq)
        {
            Touched.reserve(TradeReportReceiveBatchSize);

            // Fill with invalid SequenceId to mark as empty
            for (size_t i = 0; i < GatewayUpdateBufferSize; ++i)
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
            std::cout << "Sequence Id: " << report->SequenceId << "\n\n";

            LastSeq = report->SequenceId;

            UpdatesBuffer[report->SequenceId & (GatewayUpdateBufferSize - 1)] = *report;

            uint32_t clientFd;

            bool found = ClientsFd.GetVal(report->ClientId, clientFd);

            if (!found)
            {
                std::cout << "Not a client of this gateway, discarding.\n\n";
                return;
            }

            States.SetClientAssets(clientFd, report->BoughtDelta, 0,
                                   report->BoughtAssetId);
            States.SetClientAssets(clientFd, report->SoldDelta,
                                   report->State == OrderState::ADD
                                       ? 0 // Ignore add updates because
                                           // they're already taken into
                                           // account (local counter)
                                       : report->SoldAttemptDelta,
                                   report->SoldAssetId);
            States.FlushTripleBuffer(clientFd);
        }

        void Handle(TradeReportBatch &reportBatch, size_t batchSize) noexcept
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
                std::cout << "Sequence Id: " << report->SequenceId << "\n\n";

                LastSeq = report->SequenceId;

                UpdatesBuffer[report->SequenceId & (GatewayUpdateBufferSize - 1)] = *report;

                uint32_t clientFd;

                bool found = ClientsFd.GetVal(report->ClientId, clientFd);

                if (!found)
                {
                    std::cout
                        << "Not a client of this gateway, discarding.\n\n";
                    continue;
                }

                States.SetClientAssets(clientFd, report->BoughtDelta, 0,
                                       report->BoughtAssetId);
                States.SetClientAssets(
                    clientFd, report->SoldDelta,
                    report->State == OrderState::ADD
                        ? 0 // Ignore add updates because they're already taken
                            // into account (local counter)
                        : report->SoldAttemptDelta,
                    report->SoldAssetId);
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

        void Handle(AccountResponseBatch *responseBatch) noexcept
        {
            for (size_t i = 0; i < responseBatch->Size; ++i)
            {
                ClientAccountSnapshot &response =
                    (*responseBatch)[i];
                if (UpdatesBuffer[response.SequenceId & (GatewayUpdateBufferSize - 1)]
                            .SequenceId
                        != response.SequenceId
                    && response.SequenceId != LastSeq) [[unlikely]]
                {
                    continue;
                    // Resend the request
                    RoutedAuthRequest *gatewayRequest =
                        GatewayReqPool.Acquire();

                    if (!gatewayRequest) [[unlikely]]
                    {
                        // TODO : Handle failure to get user details
                        std::cout << "Couldn't acquire while trying to "
                                     "process an account service response.\n\n";
                    }

                    gatewayRequest->RequestType = 'A';
                    gatewayRequest->ClientId = response.ClientId;
                    gatewayRequest->ClientFd = response.ClientFd;
                    gatewayRequest->Key.fill('R');
                    gatewayRequest->GatewayId = GatewayId;

                    bool enqueued = OutgoingReq.TryPush(gatewayRequest);

                    if (!enqueued) [[unlikely]]
                    {
                        // TODO : Handle failure again
                        std::cout << "Couldn't push while trying to "
                                     "process an account service response.\n\n";
                    }

                    std::cerr << "Response sequence id is stale: "
                              << response.SequenceId << ".\n\n";

                    response.log();
                }

                // Iterate buffer to apply missed delta
                uint64_t seqId = response.SequenceId + 1;

                while (seqId <= LastSeq)
                {
                    OrderStateReport &curReport =
                        UpdatesBuffer[seqId++ & (GatewayUpdateBufferSize - 1)];

                    if (curReport.ClientId == response.ClientId)
                    {
                        for (size_t i = 0; i < MaxPositions; ++i)
                        {
                            // TODO: See what needs to be changed to make local
                            // attempt for risk check (attempt deltas need to be
                            // sometimes ignored)
                            if (response.AssetId[i] == curReport.BoughtAssetId)
                            {
                                response.Confirmed[i] += curReport.BoughtDelta;
                            }
                            else if (response.AssetId[i]
                                     == curReport.SoldAssetId)
                            {
                                response.Confirmed[i] += curReport.SoldDelta;
                                response.Attempt[i] += curReport.SoldDelta;
                            }
                        }
                    }
                }

                std::cout << "Got a connection confirmation:\n"
                          << "- Client Id: " << response.ClientId << "\n\n";

                response.log();

                uint32_t curClientId = States.GetClientId(response.ClientFd);

                if (curClientId != response.ClientId)
                {
                    ClientsFd.DeleteNode(curClientId);
                    ClientsFd.AddNode(response.ClientId, response.ClientFd);
                }

                States.SetClientState(&response);
                States.FlushTripleBuffer(response.ClientFd);

                ClientsFd.AddNode(response.ClientId, response.ClientFd);
            }
        }

        void TryConsumeDisconnects(void) noexcept
        {
            uint32_t fd;

            if (IncomingDisconnects.TryPop(fd))
            {
                States.SetAuthStatus(fd, 0);
                States.FlushTripleBuffer(fd);
            }
        }

        void StartLoop(void) noexcept
        {
            while (true)
            {
                StatesWriterReportBase::TryConsume();
                StatesWriterResponseBase::TryConsume();
                TryConsumeDisconnects();
            }
        }
    };
} // namespace naoto::order_gateway