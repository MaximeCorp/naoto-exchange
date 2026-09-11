#pragma once

#include <cerrno>
#include <chrono>
#include <client_account_snapshot.hpp>
#include <cstring>
#include <object_batch.hpp>
#include <object_buffer.hpp>
#include <readerwritercircularbuffer.h>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <sys/socket.h>
#include <thread>
#include <versioned_fd.hpp>

namespace naoto::order_gateway
{
    class AccountServiceResponseReceiver
    {
        using ResponseBatch =
            ObjectBatch<ClientAccountSnapshot<MaxPositions>,
                        GatewayClientRequestResponseBatchSize>;
        using ResponseQueue =
            SpscQueueProducer<ResponseBatch *,
                              GatewayClientRequestResponseQueueSize>;

    private:
        VersionedFd &AccountFd;
        ResponseQueue OutgoingResponses;
        StoragePool<ResponseBatch, GatewayClientRequestResponsePoolSize>
            &ResponsePool;
        ObjectBuffer<ClientAccountSnapshot<MaxPositions>> Buffer;
        uint32_t BufferGen;

        void ProcessResponses(void) noexcept
        {
            uint64_t curVal = AccountFd.load(std::memory_order_acquire);

            int32_t curFd = VersionedFd::Fd(curVal);
            uint32_t curGen = VersionedFd::Gen(curVal);

            if (curFd == -1) [[unlikely]]
            {
                // Avoid busy polling when the connection to account service
                // isn't back
                Buffer.clearBuffer();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return;
            }

            if (BufferGen != curGen) [[unlikely]]
            {
                Buffer.clearBuffer();
            }

            BufferGen = curGen;

            ResponseBatch *curBatch = ResponsePool.Acquire();

            if (!curBatch) [[unlikely]]
            {
                // Avoid busy polling when the pool is drained
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return;
            }

            if (Buffer.BufferSize > 0) [[likely]]
            {
                std::memcpy(curBatch->Data.data(), Buffer.Buffer.data(),
                            Buffer.BufferSize);
            }

            ssize_t nread =
                recv(curFd, (char *)(curBatch->Data.data()) + Buffer.BufferSize,
                     sizeof(ClientAccountSnapshot<MaxPositions>)
                             * GatewayClientRequestResponseBatchSize
                         - Buffer.BufferSize,
                     0);

            if (nread <= 0) [[unlikely]]
            {
                if (!ResponsePool.LocalRelease(curBatch)) [[unlikely]]
                {
                    // TODO : Handle this case
                }

                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    Buffer.clearBuffer();
                }

                return;
            }

            auto [batchSize, bufferSize] =
                std::div((int)(nread + Buffer.BufferSize),
                         (int)sizeof(ClientAccountSnapshot<MaxPositions>));

            curBatch->Size = batchSize;

            Buffer.clearBuffer();

            if (!Buffer.addBytes(
                    curBatch->Data.data()
                        + sizeof(ClientAccountSnapshot<MaxPositions>)
                            * batchSize,
                    bufferSize)) [[unlikely]]
            {
                std::cerr << "Unexpected buffer overflow while storing "
                             "leftover account service response bytes\n\n";
            }

            if (!OutgoingResponses.TryPush(curBatch)) [[unlikely]]
            {
                std::cout << "Failed enqueing a batch of " << curBatch->Size
                          << " account service response(s).\n\n";
                if (!ResponsePool.LocalRelease(curBatch)) [[unlikely]]
                {
                    // even worse
                }
                // TODO : handle
            }
            else
            {
                std::cout << "Successfully pushed a batch of " << curBatch->Size
                          << " responses.\n\n";
            }
        }

    public:
        AccountServiceResponseReceiver(
            VersionedFd &accountFd,
            SpscQueue<ResponseBatch *, GatewayClientRequestResponseQueueSize>
                *outgoingResponses,
            StoragePool<ResponseBatch, GatewayClientRequestResponsePoolSize>
                &responsePool)
            : AccountFd(accountFd)
            , OutgoingResponses(outgoingResponses)
            , ResponsePool(responsePool)
            , BufferGen(0)
        {}

        void StartLoop(void) noexcept
        {
            while (true)
            {
                ProcessResponses();
            }
        }
    };
} // namespace naoto::order_gateway