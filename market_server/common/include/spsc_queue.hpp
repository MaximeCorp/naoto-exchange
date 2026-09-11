#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <concepts.hpp>
#include <cstddef>
#include <cstdint>

namespace naoto
{
    template <typename T, size_t Size>
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

        [[nodiscard]] size_t GetSize(void) const noexcept
        {
            size_t head = Head.load(std::memory_order_relaxed);
            size_t tail = Tail.load(std::memory_order_relaxed);

            return tail - head;
        }

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

        [[nodiscard]] bool TimedPush(const T &item, size_t &localHead,
                                     const uint64_t timeout_us) noexcept
        {
            const size_t localTail = Tail.load(std::memory_order_relaxed);

            auto timeout = std::chrono::microseconds(timeout_us);
            auto deadline = std::chrono::high_resolution_clock::now() + timeout;

            while (localTail - localHead >= Size) [[unlikely]]
            {
                if (std::chrono::high_resolution_clock::now() >= deadline)
                    [[unlikely]]
                {
                    return false;
                }

                localHead = Head.load(std::memory_order_acquire);
            }

            const size_t idx = localTail & (Size - 1);

            Buffer[idx] = item;

            Tail.store(localTail + 1, std::memory_order_release);

            return true;
        }

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

        [[nodiscard]] bool TimedPop(T &item, size_t &localTail,
                                    const uint64_t timeout_us) noexcept
        {
            size_t localHead = Head.load(std::memory_order_relaxed);

            auto timeout = std::chrono::microseconds(timeout_us);
            auto deadline = std::chrono::high_resolution_clock::now() + timeout;

            while (localTail == localHead) [[unlikely]]
            {
                if (std::chrono::high_resolution_clock::now() >= deadline)
                    [[unlikely]]
                {
                    return false;
                }

                localTail = Tail.load(std::memory_order_acquire);
            }

            const size_t idx = localHead & (Size - 1);

            item = Buffer[idx];

            Head.store(localHead + 1, std::memory_order_release);

            return true;
        }
    };

    template <typename T, size_t Size>
    class SpscQueueProducer
    {
    private:
        SpscQueue<T, Size> *Queue;
        size_t LocalHead;

    public:
        explicit SpscQueueProducer(SpscQueue<T, Size> *queue)
            : Queue(queue)
            , LocalHead(queue->Head.load(std::memory_order_acquire))
        {}

        [[nodiscard]] size_t GetSize(void) const noexcept
        {
            return Queue->GetSize();
        }

        [[nodiscard]] bool TryPush(const T &item) noexcept
        {
            return Queue->TryPush(item, LocalHead);
        }

        void Push(const T &item) noexcept
        {
            Queue->Push(item, LocalHead);
        }

        [[nodiscard]] bool TimedPush(const T &item,
                                     const uint64_t timeout_us) noexcept
        {
            return Queue->TimedPush(item, LocalHead, timeout_us);
        }
    };

    template <typename T, size_t Size>
    class SpscQueueConsumer
    {
    private:
        SpscQueue<T, Size> *Queue;
        size_t LocalTail;

    public:
        explicit SpscQueueConsumer(SpscQueue<T, Size> *queue)
            : Queue(queue)
            , LocalTail(queue->Tail.load(std::memory_order_acquire))
        {}

        [[nodiscard]] size_t GetSize(void) const noexcept
        {
            return Queue->GetSize();
        }

        [[nodiscard]] bool TryPop(T &item) noexcept
        {
            return Queue->TryPop(item, LocalTail);
        }

        void Pop(T &item) noexcept
        {
            Queue->Pop(item, LocalTail);
        }

        [[nodiscard]] bool TimedPop(T &item, const uint64_t timeout_us) noexcept
        {
            return Queue->TimedPop(item, LocalTail, timeout_us);
        }
    };
} // namespace naoto