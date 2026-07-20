#pragma once

#include <ReaderWriterCircularBuffer.hpp>
#include <StoragePool.hpp>

namespace MarketExecution
{
    template <typename DerivedConsumer, typename T>
    concept HasHandle = requires(DerivedConsumer d, T *item) {
        { d.handle(item) } -> std::same_as<void>;
    };

    template <typename DerivedConsumer, typename T>
        requires HasHandle<DerivedConsumer, T>
    class Consumer
    {
        using TQueue = moodycamel::BlockingReaderWriterCircularBuffer<T *>;

    protected:
        TQueue &Incoming;
        StoragePool<T> &Mempool;

        [[nodiscard]] bool FreeElement(
            T *element) noexcept // Caller"s responsability to check pointer
        {
            return Mempool.release(element);
        }

    public:
        Consumer(TQueue &incoming, StoragePool<T> &mempool);

        void TryConsume(void) noexcept
        {
            T *curElement = nullptr;

            if (!Incoming.try_dequeue(curElement)) [[unlikely]]
            {
                return;
            }

            static_cast<DerivedConsumer *>(this)->Handle(curElement);

            FreeElement(curElement);
        }

        void Consume(void) noexcept
        {
            T *curElement = nullptr;

            Incoming.wait_dequeue(curElement);

            static_cast<DerivedConsumer *>(this)->Handle(curElement);

            FreeElement(curElement);
        }
    };
} // namespace MarketExecution
