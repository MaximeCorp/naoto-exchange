#pragma once

#include <array>
#include <spsc_queue.hpp>
#include <storage_pool.hpp>
#include <variant>

namespace naoto
{
    template <typename DerivedConsumer, typename T>
    concept HasHandle = requires(DerivedConsumer d, T *item) {
        { d.Handle(item) } -> std::same_as<void>;
    };

    template <typename DerivedConsumer, typename T, size_t BatchSize = 0>
    concept HasBatchHandle = requires(
        DerivedConsumer d, std::array<T *, BatchSize> &batch, size_t curSize) {
        { d.Handle(batch, curSize) } -> std::same_as<void>;
    };

    template <typename DerivedConsumer, typename T, size_t BatchSize>
    concept ValidConsumerHandler =
        ((BatchSize == 0 && HasHandle<DerivedConsumer, T>)
         || (BatchSize > 0 && HasBatchHandle<DerivedConsumer, T, BatchSize>));

    // Queue a Consumer pops from, and the batch it accumulates into.
    // Unbatched (BatchSize == 0) consumers carry no buffer at all.
    template <typename T, size_t QueueSize>
    using ConsumerQueue = SpscQueueConsumer<T *, QueueSize>;

    template <typename T, size_t BatchSize>
    using ConsumerBuffer =
        std::conditional_t<(BatchSize > 0), std::array<T *, BatchSize>,
                           std::monostate>;

    template <typename T, size_t PoolSize>
    using ConsumerMempool = StoragePool<T, PoolSize>;

    template <typename DerivedConsumer, typename T, size_t QueueSize,
              size_t PoolSize, size_t BatchSize = 0, size_t Tag = 0>
    class Consumer
    {
    protected:
        ConsumerQueue<T, QueueSize> Incoming;
        ConsumerMempool<T, PoolSize> &Mempool;
        [[no_unique_address]] ConsumerBuffer<T, BatchSize> Buffer{};

        [[nodiscard]] bool FreeElement(
            T *element) noexcept // Caller"s responsability to check pointer
        {
            return Mempool.Release(element);
        }

    public:
        Consumer(SpscQueue<T *, QueueSize> *incoming,
                 ConsumerMempool<T, PoolSize> &mempool)
            : Incoming(incoming)
            , Mempool(mempool)
        {}

        void TryConsume(void) noexcept
        {
            static_assert(ValidConsumerHandler<DerivedConsumer, T, BatchSize>,
                          "DerivedConsumer must provide a matching Handle()");
            if constexpr (BatchSize > 0)
            {
                T *curElement = nullptr;
                size_t curSize = 0;

                while (curSize < BatchSize && Incoming.TryPop(curElement))
                {
                    Buffer[curSize++] = curElement;
                }

                if (curSize == 0) [[unlikely]]
                {
                    return;
                }

                static_cast<DerivedConsumer *>(this)->Handle(Buffer, curSize);

                for (size_t i = 0; i < curSize; ++i)
                {
                    bool released = FreeElement(Buffer[i]);

                    if (!released) [[unlikely]]
                    {
                        std::cerr << "Failed to release a mempool element\n";
                        std::terminate();
                    }
                }
            }
            else
            {
                T *curElement = nullptr;

                if (!Incoming.TryPop(curElement)) [[unlikely]]
                {
                    return;
                }

                static_cast<DerivedConsumer *>(this)->Handle(curElement);

                bool released = FreeElement(curElement);

                if (!released) [[unlikely]]
                {
                    std::cerr << "Failed to release a mempool element\n";
                    std::terminate();
                }
            }
        }

        void Consume(void) noexcept // Not opportunistic batching
        {
            static_assert(ValidConsumerHandler<DerivedConsumer, T, BatchSize>,
                          "DerivedConsumer must provide a matching Handle()");
            if constexpr (BatchSize > 0)
            {
                T *curElement = nullptr;
                size_t curSize = 0;

                while (curSize < BatchSize)
                {
                    Incoming.Pop(curElement);
                    Buffer[curSize++] = curElement;
                }

                static_cast<DerivedConsumer *>(this)->Handle(Buffer, curSize);

                for (size_t i = 0; i < curSize; ++i)
                {
                    bool released = FreeElement(Buffer[i]);

                    if (!released) [[unlikely]]
                    {
                        std::cerr << "Failed to release a mempool element\n";
                        std::terminate();
                    }
                }
            }
            else
            {
                T *curElement = nullptr;

                Incoming.Pop(curElement);

                static_cast<DerivedConsumer *>(this)->Handle(curElement);

                bool released = FreeElement(curElement);

                if (!released) [[unlikely]]
                {
                    std::cerr << "Failed to release a mempool element\n";
                    std::terminate();
                }
            }
        }

        void ConsumeTimed(const uint32_t delay_us) noexcept
        {
            static_assert(ValidConsumerHandler<DerivedConsumer, T, BatchSize>,
                          "DerivedConsumer must provide a matching Handle()");
            if constexpr (BatchSize > 0)
            {
                T *curElement = nullptr;
                size_t curSize = 0;

                while (curSize < BatchSize)
                {
                    if (!Incoming.TimedPop(curElement, delay_us)) [[unlikely]]
                    {
                        break;
                    }

                    Buffer[curSize++] = curElement;
                }

                static_cast<DerivedConsumer *>(this)->Handle(Buffer, curSize);

                for (size_t i = 0; i < curSize; ++i)
                {
                    bool released = FreeElement(Buffer[i]);

                    if (!released) [[unlikely]]
                    {
                        std::cerr << "Failed to release a mempool element\n";
                        std::terminate();
                    }
                }
            }
            else
            {
                T *curElement = nullptr;

                if (!Incoming.TimedPop(curElement, delay_us)) [[unlikely]]
                {
                    return;
                }

                static_cast<DerivedConsumer *>(this)->Handle(curElement);

                bool released = FreeElement(curElement);

                if (!released) [[unlikely]]
                {
                    std::cerr << "Failed to release a mempool element\n";
                    std::terminate();
                }
            }
        }
    };
} // namespace naoto
