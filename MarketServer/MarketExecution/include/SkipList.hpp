#include <UnsafeStoragePool.hpp>
#include <array>
#include <cstdlib>
#include <limits>
#include <queue>

namespace MarketExecution
{
    template <typename K, typename V, size_t MaxLevel>
    class SkipList
    {
    public:
        struct SkipNode
        {
            K Key;
            V *Value;
            size_t Height;
            std::array<SkipNode *, MaxLevel + 1> Forward;

            SkipNode(K &key)
                : Key(key)
                , Value(nullptr)
                , Height(MaxLevel)
            {
                Forward.fill(nullptr);
            }
        };

    private:
        SkipNode *Sentinel;
        SkipNode *Tail;
        UnsafeStoragePool<SkipNode> NodesPool;
        uint64_t State;

        [[nodiscard]] int nextLevel(void) noexcept
        {
            State ^= State << 13;
            State ^= State >> 7;
            State ^= State << 17;

            int level = std::countr_zero(State);

            return (level <= MaxLevel) ? level : MaxLevel;
        }

    public:
        SkipList(UnsafeStoragePool<V> &dataPool, std::queue<V *> &deleteQueue)
            : Sentinel(nullptr)
            , Tail(nullptr)
            , DataPool(dataPool)
            , NodesPool(DataPool.getCapacity())
            , DeleteQueue(deleteQueue)
            , State(__rdtsc())
        {
            Tail = NodesPool.acquire();
            Tail->Key = std::numeric_limits<K>::max();
            Tail->Value = nullptr;
            Tail->Height = MaxLevel;
            Tail->Forward.fill(nullptr);

            Sentinel = NodesPool.acquire();
            Sentinel->Key = std::numeric_limits<K>::min();
            Sentinel->Value = nullptr;
            Sentinel->Height = MaxLevel;
            Sentinel->Forward.fill(Tail);
        }

        SkipList(const size_t nodesPoolSize, UnsafeStoragePool<V> &dataPool,
                 std::queue<V *> &deleteQueue)
            : Sentinel(nullptr)
            , Tail(nullptr)
            , DataPool(dataPool)
            , NodesPool(nodesPoolSize)
            , DeleteQueue(deleteQueue)
            , State(__rdtsc())
        {
            Tail = NodesPool.acquire();
            Tail->Key = std::numeric_limits<K>::max();
            Tail->Value = nullptr;
            Tail->Height = MaxLevel;
            Tail->Forward.fill(nullptr);

            Sentinel = NodesPool.acquire();
            Sentinel->Key = std::numeric_limits<K>::min();
            Sentinel->Value = nullptr;
            Sentinel->Height = MaxLevel;
            Sentinel->Forward.fill(Tail);
        }

        ~SkipList()
        {
            SkipNode *curNode = Sentinel;

            while (curNode)
            {
                SkipNode *toRelease = curNode;
                curNode = curNode->Forward[0];
                NodesPool.release(toRelease);
            }
        }

        [[nodiscard]] V *GetVal(const K &key) noexcept
        {
            SkipNode *curNode = Sentinel;

            for (int curLevel = MaxLevel; curLevel >= 0; --curLevel)
            {
                while (curNode->Forward[curLevel]->Key < key)
                {
                    curNode = curNode->Forward[curLevel];
                }
            }

            curNode = curNode->Forward[0];

            if (curNode->Key != key) [[unlikely]]
            {
                return nullptr;
            }

            return curNode->Value;
        }

        void AddNode(V *val) noexcept
        {
            const K *key = val->GetKey();

            std::array<SkipNode *, MaxLevel + 1> prev;

            SkipNode *curNode = Sentinel;

            for (int curLevel = MaxLevel; curLevel >= 0; --curLevel)
            {
                while (curNode->Forward[curLevel]->Key < key)
                {
                    curNode = curNode->Forward[curLevel];
                }

                prev[curLevel] = curNode;
            }

            std::uint64_t level = nextLevel();
            SkipNode *__restrict newNode = NodesPool.acquire();
            newNode->Key = key;
            newNode->Value = val;
            newNode->Height = level;

            for (size_t i = 0; i <= level; ++i)
            {
                newNode->Forward[i] = prev[i]->Forward[i];
                prev[i]->Forward[i] = newNode;
            }

            for (size_t i = level + 1; i <= MaxLevel; ++i)
            {
                newNode->Forward[i] = Tail;
            }
        }

        void DeleteNode(K &key) noexcept
        {
            std::array<SkipNode *, MaxLevel + 1> prev;

            SkipNode *curNode = Sentinel;

            for (int curLevel = MaxLevel; curLevel >= 0; --curLevel)
            {
                while (curNode->Forward[curLevel]->Key < key)
                {
                    curNode = curNode->Forward[curLevel];
                }

                prev[curLevel] = curNode;
            }

            curNode = curNode->Forward[0];

            if (curNode->Key == key) [[likely]]
            {
                for (size_t i = 0; i <= curNode->Height; i++)
                {
                    prev[i]->Forward[i] = curNode->Forward[i];
                }

                DataPool.release(curNode->Value);
                NodesPool.release(curNode);
            }
        }
    };
} // namespace MarketExecution
