#pragma once

#include <cerrno>
#include <chrono>
#include <client_account_snapshot.hpp>
#include <cstring>
#include <versioned_fd.hpp>
#include <object_batch.hpp>
#include <object_buffer.hpp>
#include <readerwritercircularbuffer.h>
#include <storage_pool.hpp>
#include <sys/socket.h>
#include <thread>

namespace naoto::order_gateway
{
    template <size_t BatchSize, size_t MaxPositions>
    class AccountServiceResponseReceiver
    {
        using ResponseBatch =
            ObjectBatch<ClientAccountSnapshot<MaxPositions>, BatchSize>;
        using ResponseQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<ResponseBatch *>;

    private:
        VersionedFd &AccountFd;
        ResponseQueue &OutgoingResponses;
        StoragePool<ResponseBatch> &ResponsePool;
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

            ResponseBatch *curBatch = ResponsePool.acquire();

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
                     sizeof(ClientAccountSnapshot<MaxPositions>) * BatchSize
                         - Buffer.BufferSize,
                     0);

            if (nread <= 0) [[unlikely]]
            {
                if (!ResponsePool.localRelease(curBatch)) [[unlikely]]
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

            if (!OutgoingResponses.try_enqueue(curBatch)) [[unlikely]]
            {
                std::cout << "Failed enqueing a batch of " << curBatch->Size
                          << " account service response(s).\n\n";
                if (!ResponsePool.localRelease(curBatch)) [[unlikely]]
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
        AccountServiceResponseReceiver(VersionedFd &accountFd,
                                       ResponseQueue &outgoingResponses,
                              StoragePool<ResponseBatch> &responsePool)
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