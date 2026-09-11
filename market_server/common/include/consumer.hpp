#pragma once

// TODO : update theinclude for moodycamel spsc queue
#include <array>
#include <readerwritercircularbuffer.h>
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

    template <typename DerivedConsumer, typename T, size_t QueueSize,
              size_t PoolSize, size_t BatchSize = 0, size_t Tag = 0>
    class Consumer
    {
        using TQueue = SpscQueueConsumer<T *, QueueSize>;
        using BufferType =
            std::conditional_t<(BatchSize > 0), std::array<T *, BatchSize>,
                               std::monostate>;

    protected:
        TQueue Incoming;
        StoragePool<T, PoolSize> &Mempool;
        [[no_unique_address]] BufferType Buffer{};

        [[nodiscard]] bool FreeElement(
            T *element) noexcept // Caller"s responsability to check pointer
        {
            return Mempool.Release(element);
        }

    public:
        Consumer(SpscQueue<T *, QueueSize> *incoming,
                 StoragePool<T, PoolSize> &mempool)
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
