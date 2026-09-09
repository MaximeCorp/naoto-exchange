#pragma once

#include <array>
#include <atomic>
#include <concepts.hpp>
#include <cstddef>

namespace naoto
{
    template <size_t Size, typename T>
        requires PowerOfTwo<Size>
    struct SpscQueue
    {
        alignas(64) std::atomic<size_t> Head;
        alignas(64) std::atomic<size_t> Tail;
        alignas(64) std::array<T, Size> Buffer;

        SpscQueue(void)
            : Head(0)
            , Tail(0)
        {}

        [[nodiscard]] bool TryPush(const T &item, size_t &localHead) noexcept
        {
            const size_t localTail = Tail.load(std::memory_order_relaxed);

            if (localTail - localHead >= Size) [[unlikely]]
            {
                localHead = Head.load(std::memory_order_acquire);

                if (localTail - localHead >= Size)
                {
                    return false;
                }
            }

            const size_t idx = localTail & (Size - 1);

            Buffer[idx] = item;

            Tail.store(localTail + 1, std::memory_order_release);

            return true;
        }

        void Push(const T &item, size_t &localHead) noexcept
        {
            const size_t localTail = Tail.load(std::memory_order_relaxed);

            while (localTail - localHead >= Size) [[unlikely]]
            {
                localHead = Head.load(std::memory_order_acquire);
            }

            const size_t idx = localTail & (Size - 1);

            Buffer[idx] = item;

            Tail.store(localTail + 1, std::memory_order_release);
        };

        [[nodiscard]] bool TryPop(T &item, size_t &localTail) noexcept
        {
            size_t localHead = Head.load(std::memory_order_relaxed);

            if (localTail == localHead) [[unlikely]]
            {
                localTail = Tail.load(std::memory_order_acquire);

                if (localTail == localHead)
                {
                    return false;
                }
            }

            const size_t idx = localHead & (Size - 1);

            item = Buffer[idx];

            Head.store(localHead + 1, std::memory_order_release);

            return true;
        }

        void Pop(T &item, size_t &localTail) noexcept
        {
            size_t localHead = Head.load(std::memory_order_relaxed);

            while (localTail == localHead) [[unlikely]]
            {
                localTail = Tail.load(std::memory_order_acquire);
            }

            const size_t idx = localHead & (Size - 1);

            item = Buffer[idx];

            Head.store(localHead + 1, std::memory_order_release);
        }
    };

    template <size_t Size, typename T>
    class SpscQueueProducer
    {
    private:
        SpscQueue<Size, T> *Queue;
        size_t LocalHead;

    public:
        explicit SpscQueueProducer(SpscQueue<Size, T> *queue)
            : Queue(queue)
            , LocalHead(queue->Head.load(std::memory_order_acquire))
        {}

        [[nodiscard]] bool TryPush(const T &item) noexcept
        {
            return Queue->TryPush(item, LocalHead);
        }

        void Push(const T &item) noexcept
        {
            Queue->Push(item, LocalHead);
        }
    };

    template <size_t Size, typename T>
    class SpscQueueConsumer
    {
    private:
        SpscQueue<Size, T> *Queue;
        size_t LocalTail;

    public:
        explicit SpscQueueConsumer(SpscQueue<Size, T> *queue)
            : Queue(queue)
            , LocalTail(queue->Tail.load(std::memory_order_acquire))
        {}

        [[nodiscard]] bool TryPop(const T &item) noexcept
        {
            return Queue->TryPop(item, LocalTail);
        }

        void Pop(T &item) noexcept
        {
            Queue->Pop(item, LocalTail);
        }
    };
} // namespace naoto