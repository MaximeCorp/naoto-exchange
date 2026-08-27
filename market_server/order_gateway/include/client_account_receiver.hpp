#pragma once

#include <client_request_response.hpp>
#include <fd_gen.hpp>
#include <object_batch.hpp>
#include <object_buffer.hpp>
#include <reader_writer_circular_buffer.hpp>
#include <storage_pool.hpp>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <thread>

namespace Gateways
{
    template <size_t BatchSize, size_t MaxPositions>
    class ClientAccountReceiver
    {
        using ResponseBatch =
            ObjectBatch<ClientRequestResponse<MaxPositions>, BatchSize>;
        using ResponseQueue =
            moodycamel::BlockingReaderWriterCircularBuffer<ResponseBatch *>;

    private:
        FdGen &CdpFd;
        ResponseQueue &OutgoingResponses;
        StoragePool<ResponseBatch> &ResponsePool;
        ObjectBuffer<ClientRequestResponse<MaxPositions>> Buffer;
        uint32_t BufferGen;

        void ProcessResponses(void) noexcept
        {
            uint64_t curVal = CdpFd.load(std::memory_order_acquire);

            int32_t curFd = FdGen::Fd(curVal);
            uint32_t curGen = FdGen::Gen(curVal);

            if (curFd == -1) [[unlikely]]
            {
                // Avoid busy polling when the connection to cdp isn't back
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
                     sizeof(ClientRequestResponse<MaxPositions>) * BatchSize
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
                         (int)sizeof(ClientRequestResponse<MaxPositions>));

            curBatch->Size = batchSize;

            Buffer.clearBuffer();
            Buffer.addBytes(curBatch->Data.data()
                                + sizeof(ClientRequestResponse<MaxPositions>)
                                    * batchSize,
                            bufferSize);

            if (!OutgoingResponses.try_enqueue(curBatch)) [[unlikely]]
            {
                std::cout << "Failed enqueing a batch of " << curBatch->Size
                          << " cdp response(s).\n\n";
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
        ClientAccountReceiver(FdGen &cdpFd, ResponseQueue &outgoingResponses,
                              StoragePool<ResponseBatch> &responsePool)
            : CdpFd(cdpFd)
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
} // namespace Gateways