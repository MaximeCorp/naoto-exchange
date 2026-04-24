#include <UnsafeStoragePool.hpp>
#include <array>
#include <cstdlib>
#include <queue>

namespace MarketExecution
{
    template <typename K, typename V, size_t MaxLevel>
    class SkipList
    {
        template <typename K, typename V, size_t MaxLevel>
        struct SkipNode
        {
            K Key;
            V *Value;

            size_t Height;

            std::array<SkipNode<V>> Forward;

            // Create constructor (in which you set size of )
        };

    private:
        std::array<SkipNode<K, V, MaxLevel> *> Heads;
        UnsafeStoragePool<V> &DataPool;
        UnsafeStoragePool<SkipNode<K, V, MaxLevel>> NodesPool;
        std::queue<SkipNode<K, V, MaxLevel> *> &DeleteQueue;

    public:
        SkipList<K, V, size_t>(UnsafeStoragePool<V> &dataPool,
                               std::queue<V *> &deleteQueue)
            : DataPool(dataPool)
            , NodesPool(DataPool.getCapacity())
            , DeleteQueue(deleteQueue)
        {
            Heads.resize(MaxLevel);

            for (size_t i = 0; i < MaxLevel; ++i)
            {
                Heads[i] = nullptr;
            }
        }

        SkipList<K, V, size_t>(size_t nodesPoolSize,
                               UnsafeStoragePool<V> &dataPool,
                               std::queue<V *> &deleteQueue)
            : DataPool(dataPool)
            , NodesPool(nodesPoolSize)
            , DeleteQueue(deleteQueue)
        {
            Heads.resize(MaxLevel);

            for (size_t i = 0; i < MaxLevel; ++i)
            {
                Heads[i] = nullptr;
            }
        }

        Add
    };
} // namespace MarketExecution
