#pragma once

#include <client_account_snapshot.hpp>
#include <cstring>
#include <object_batch.hpp>
#include <readerwritercircularbuffer.h>
#include <routed_message.hpp>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <sys/socket.h>
#include <system_conf.hpp>
#include <type_traits>
#include <versioned_fd.hpp>

namespace naoto::account_service
{
    template <size_t MaxPositions, size_t BatchesSize, size_t MaxGateways,
              size_t ResendBufferSize>
    class GatewayResponseDispatcher
    {
        // Might be worth batching
        using ResponsesQueue = SpscQueueConsumer<
            RoutedMessage<ClientAccountSnapshot<MaxPositions>>, MaxClients>;
        // using UpdatesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
        // RoutedMessage<ClientUpdate>>;

    private:
        ResponsesQueue Responses;
        // UpdatesQueue &Updates;
        StoragePool<ClientAccountSnapshot<MaxPositions>,
                    AccountResponsePoolSize> &ResponsesPool;
        // StoragePool<ClientUpdate> &UpdatesPool;
        std::array<VersionedFd, MaxGateways> &GatewayFd; // Consumer

        std::array<RoutedMessage<ClientAccountSnapshot<MaxPositions>>,
                   ResendBufferSize>
            ResponsesResend;
        size_t ResponsesResendSize;

        template <typename T>
        void DrainResendBuffers(void) noexcept // Call before SendMessage if
                                               // order of messages matters
        {
            if constexpr (std::is_same_v<T,
                                         ClientAccountSnapshot<MaxPositions>>)
            {
                while (ResponsesResendSize)
                {
                    RoutedMessage<ClientAccountSnapshot<MaxPositions>>
                        &curResponse = ResponsesResend[--ResponsesResendSize];

                    SendMessage<RoutedMessage>(curResponse.Message,
                                               curResponse.GatewayId);
                }
            }
        }

        template <typename T>
        void SendMessage(const T *curMessage, size_t idx) noexcept
        {
            VersionedFd &curSlot = GatewayFd[idx];
            uint64_t curVal =
                curSlot.load(std::memory_order_relaxed); // Relaxed because
                                                         // memory dependancy
                                                         // allows it

            std::cout << "curVal is " << (int32_t)curVal << "\n";

            int32_t curFd = VersionedFd::Fd(curVal);

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

            std::cerr << "Sending message to gateway " << idx
                      << ", size=" << sizeof(T) << ", sent=" << sent << "\n";

            if (sent < 0) [[unlikely]]
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if constexpr (std::is_same_v<
                                      T, ClientAccountSnapshot<MaxPositions>>)
                    {
                        ResponsesResend[ResponsesResendSize].GatewayId = idx;
                        ResponsesResend[ResponsesResendSize++].Message =
                            (T *)curMessage;
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
                if constexpr (std::is_same_v<
                                  T, ClientAccountSnapshot<MaxPositions>>)
                {
                    ResponsesResend[ResponsesResendSize].GatewayId = idx;
                    ResponsesResend[ResponsesResendSize++].Message =
                        (T *)curMessage;
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
            if constexpr (std::is_same_v<T,
                                         ClientAccountSnapshot<MaxPositions>>)
            {
                bool released = ResponsesPool.Release((T *)curMessage);

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
            // TODO : decide if this batching is useful
            for (size_t i = 0; i < BatchesSize; ++i)
            {
                RoutedMessage<ClientAccountSnapshot<MaxPositions>> curResponse;

                if (!Responses.TryPop(curResponse)) [[unlikely]]
                {
                    break;
                }

                SendMessage<ClientAccountSnapshot<MaxPositions>>(
                    curResponse.Message, curResponse.GatewayId);
            }

            /*
            for (size_t i = 0; i < BatchesSize; ++i)
            {
                const RoutedMessage<ClientUpdate> curUpdate;

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
        GatewayResponseDispatcher(
            SpscQueue<RoutedMessage<ClientAccountSnapshot<MaxPositions>>,
                      MaxClients> *responses, // UpdatesQueue &updates,
            StoragePool<ClientAccountSnapshot<MaxPositions>,
                        AccountResponsePoolSize> &responsesPool, //,
            // StoragePool<ClientUpdate> &updatesPool,
            std::array<VersionedFd, MaxGateways> &gatewayFd)
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
} // namespace naoto::account_service
