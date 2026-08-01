#pragma once

#include <ClientRequestResponse.hpp>
#include <ClientUpdate.hpp>
#include <FdGen.hpp>
#include <MessageContainer.hpp>
#include <ObjectBatch.hpp>
#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>
#include <cstring>
#include <sys/socket.h>
#include <type_traits>

namespace AccountService
{
    template <size_t MaxPositions, size_t BatchesSize, size_t MaxGateways,
              size_t ResendBufferSize>
    class GatewayWriter
    {
        // Might be worth batching
        using ResponsesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            MessageContainer<ClientRequestResponse<MaxPositions>>>;
        using UpdatesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
            MessageContainer<ClientUpdate>>;

    private:
        ResponsesQueue &Responses;
        // UpdatesQueue &Updates;
        StoragePool<ClientRequestResponse<MaxPositions>> &ResponsesPool;
        // StoragePool<ClientUpdate> &UpdatesPool;
        std::array<FdGen, MaxGateways> &GatewayFd; // Consumer

        std::array<MessageContainer<ClientRequestResponse<MaxPositions>>,
                   ResendBufferSize>
            ResponsesResend;
        size_t ResponsesResendSize;

        /*
        std::array<ClientUpdate, ResendBufferSize> UpdatesResend;
        size_t UpdatesResendSize;
        */

        template <typename T>
        void DrainResendBuffers(void) noexcept // Call before SendMessage if
                                               // order of messages matters
        {
            if constexpr (std::is_same_v<T, ClientRequestResponse>)
            {
                while (ResponsesResendSize)
                {
                    MessageContainer<ClientRequestResponse<MaxPositions>>
                        &curResponse = ResponsesResend[--ResponsesResendSize];

                    SendMessage<MessageContainer>(curResponse.Message,
                                                  curResponse.GatewayId);
                }
            }
            /*
            else if constexpr (std::is_same_v<T, ClientUpdate>)
            {
                while (UpdatesResendSize)
                {
                    MessageContainer<ClientUpdate> &curUpdate =
                        UpdatesResend[--UpdatesResendSize];

                    SendMessage<ClientUpdate>(curUpdate.Message,
                                              curUpdate.GatewayId);
                }
            }
            */
        }

        template <typename T>
        void SendMessage(const T *curMessage, size_t idx) noexcept
        {
            FdGen &curSlot = GatewayFd[idx];
            uint64_t curVal =
                curSlot.load(std::memory_order_relaxed); // Relaxed because
                                                         // memory dependancy
                                                         // allows it

            std::cout << "(int32_t)curVal is " << (int32_t)curVal << "\n";

            int32_t curFd = FdGen::Fd(curVal);

            if (curFd == -1) [[unlikely]]
            {
                std::cout << "no gateway with id " << idx << "\n";
                return;
            }

            // edge case: if fd gets closed then recycled by and the
            // new fd is for client, then information leak, handle
            // this by closing fd after making sure the sender has
            // seen the new fd

            ssize_t sent = send(curFd, curMessage, sizeof(T), MSG_NOSIGNAL);

            std::cerr << "Sending order to fd " << curFd
                      << ", size=" << sizeof(T) << ", sent=" << sent << "\n";

            if (sent < 0) [[unlikely]]
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if constexpr (std::is_same_v<T, ClientRequestResponse>)
                    {
                        ResponsesResend[ResponsesResendSize].GatewayId = idx;
                        ResponsesResend[ResponsesResendSize++].Message =
                            curMessage;
                    }
                    /*
                    else if constexpr (std::is_same_v<T, ClientUpdate>)
                    {
                        UpdatesResend[UpdatesResendSize].GatewayId = idx;
                        UpdatesResend[UpdatesResendSize++].Message = curMessage;
                    }
                    */
                    return;
                }
                // EPIPE / ECONNRESET / other: connection dead
                return;
            }
            // edge case: send < sizeof(Order)

            uint64_t newVal = curSlot.load(std::memory_order_acquire);

            // Send again if fd has changed
            if (newVal != curVal) [[unlikely]]
            {
                // Means the send was potentially sent to the wrong
                // fd
                // For later : push to the array / vector of
                // messages to send again
                if constexpr (std::is_same_v<T, ClientRequestResponse>)
                {
                    ResponsesResend[ResponsesResendSize].GatewayId = idx;
                    ResponsesResend[ResponsesResendSize++].Message = curMessage;
                }
                /*
                else if constexpr (std::is_same_v<T, ClientUpdate>)
                {
                    UpdatesResend[UpdatesResendSize].GatewayId = idx;
                    UpdatesResend[UpdatesResendSize++].Message = curMessage;
                }
                */

                return;
            }

            // Release the message if no sending error
            if constexpr (std::is_same_v<T, ClientRequestResponse>)
            {
                bool released = ResponsesPool.release(curMessage);

                if (!released) [[unlikely]]
                {
                    std::cerr << "Pool release failed\n";
                    std::terminate();
                }
            }
            /*
            else if constexpr (std::is_same_v<T, ClientUpdate>)
            {
                bool released = UpdatesPool.release(curMessage);

                if (!released) [[unlikely]]
                {
                    std::cerr << "Pool release failed\n";
                    std::terminate();
                }
            }
            */
        }

        void ConsumeMessages(void) noexcept
        {
            for (size_t i = 0; i < BatchesSize; ++i)
            {
                const MessageContainer<ClientRequestResponse<MaxPositions>>
                    curResponse;

                if (!Responses.try_dequeue(curResponse)) [[unlikely]]
                {
                    break;
                }

                SendMessage<ClientRequestResponse<MaxPositions>>(
                    curResponse.Message, curResponse.GatewayId);
            }

            /*
            for (size_t i = 0; i < BatchesSize; ++i)
            {
                const MessageContainer<ClientUpdate> curUpdate;

                if (!Updates.try_dequeue(curUpdate)) [[unlikely]]
                {
                    break;
                }

                SendMessage<ClientUpdate>(curUpdate.Message,
                                          curUpdate.GatewayId);
            }
            */
        }

    public:
        GatewayWriter(
            ResponsesQueue &responses, // UpdatesQueue &updates,
            StoragePool<ClientRequestResponse<MaxPositions>> &responsesPool, //,
            // StoragePool<ClientUpdate> &updatesPool,
            std::array<FdGen, MaxGateways> &gatewayFd)
            : Responses(responses)
            //, Updates(updates)
            , ResponsesPool(responsesPool)
            , GatewayFd(gatewayFd)
            //, UpdatesPool(updatesPool)
            , ResponsesResendSize(0)
        {}

        void StartLoop(void) noexcept
        {
            while (true)
            {
                ConsumeMessages();
            }
        }
    };
} // namespace AccountService
