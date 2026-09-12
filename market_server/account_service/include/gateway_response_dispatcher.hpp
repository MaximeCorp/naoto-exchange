#pragma once

#include <account_service_types.hpp>
#include <client_account_snapshot.hpp>
#include <cerrno>
#include <cstring>
#include <exception>
#include <iostream>
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
    class GatewayResponseDispatcher
    {
        // Might be worth batching
        // using UpdatesQueue = moodycamel::BlockingReaderWriterCircularBuffer<
        // RoutedMessage<ClientUpdate>>;

    private:
        AccountResponseConsumer Responses;
        // UpdatesQueue &Updates;
        AccountResponseMempool &ResponsesPool;
        // StoragePool<ClientUpdate> &UpdatesPool;
        GatewayFds &GatewayFd; // Consumer

        AccountResponseResendBuffer ResponsesResend;
        size_t ResponsesResendSize;

        template <typename T>
        void DrainResendBuffers(void) noexcept // Call before SendMessage if
                                               // order of messages matters
        {
            if constexpr (std::is_same_v<T,
                                         ClientAccountSnapshot>)
            {
                while (ResponsesResendSize)
                {
                    AccountResponse &curResponse =
                        ResponsesResend[--ResponsesResendSize];

                    // ASSUMPTION: was SendMessage<RoutedMessage>, which
                    // can't compile (class template used as a type).
                    // Message is a ClientAccountSnapshot*, so that's T.
                    SendMessage<ClientAccountSnapshot>(curResponse.Message,
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
                                      T, ClientAccountSnapshot>)
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
                                  T, ClientAccountSnapshot>)
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
                                         ClientAccountSnapshot>)
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
            for (size_t i = 0; i < AccountResponseBatchSize; ++i)
            {
                AccountResponse curResponse;

                if (!Responses.TryPop(curResponse)) [[unlikely]]
                {
                    break;
                }

                SendMessage<ClientAccountSnapshot>(
                    curResponse.Message, curResponse.GatewayId);
            }

            /*
            for (size_t i = 0; i < AccountResponseBatchSize; ++i)
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
            AccountResponseQueue *responses, // UpdatesQueue &updates,
            AccountResponseMempool &responsesPool, //,
            // StoragePool<ClientUpdate> &updatesPool,
            GatewayFds &gatewayFd)
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
