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
            V Value;
            size_t Height;
            std::array<SkipNode *, MaxLevel + 1> Forward;

            SkipNode() = default;

            SkipNode(K &key)
                : Key(key)
                , Value(nullptr)
                , Height(MaxLevel)
            {
                Forward.fill(nullptr);
            }
        };

    private:
        SkipNode *Head;
        SkipNode *Tail;
        UnsafeStoragePool<SkipNode> NodesPool;
        uint64_t State;
        size_t CurMax;
        static inline uint64_t next_u64(const uint64_t state)
        {
            uint64_t newState = state + 0xa0761d6478bd642f;
            __uint128_t t =
                (__uint128_t)newState * (newState ^ 0xe7037ed1a0b428db);
            return (uint64_t)(t >> 64) ^ (uint64_t)t;
        }

        [[nodiscard]] int nextLevel(void) noexcept
        {
            State = next_u64(State);

            size_t level = __builtin_ctzll(State);

            return (level <= MaxLevel) ? level : MaxLevel;
        }

    public:
        SkipList(const size_t nodesPoolSize)
            : Head(nullptr)
            , Tail(nullptr)
            , NodesPool(nodesPoolSize + 2) // The head and the tail
            , State(__rdtsc())
            , CurMax(0)
        {
            Tail = NodesPool.acquire();
            Tail->Key = std::numeric_limits<K>::max();
            Tail->Height = MaxLevel;
            Tail->Forward.fill(nullptr);

            Head = NodesPool.acquire();
            Head->Key = std::numeric_limits<K>::min();
            Head->Height = MaxLevel;
            Head->Forward.fill(Tail);
        }

        ~SkipList()
        {
            SkipNode *curNode = Head;

            while (curNode)
            {
                SkipNode *toRelease = curNode;
                curNode = curNode->Forward[0];
                bool released = NodesPool.release(toRelease);

                if (!released) [[unlikely]]
                {
                    std::terminate();
                }
            }
        }

        [[nodiscard]] V &GetVal(const K &key, bool &found) noexcept
        {
            SkipNode *curNode = Head;

            for (int curLevel = MaxLevel; curLevel >= 0; --curLevel)
            {
                while (curNode->Forward[curLevel]->Key < key)
                {
                    __builtin_prefetch(
                        &curNode->Forward[curLevel]->Forward[curLevel]->Key);
                    curNode = curNode->Forward[curLevel];
                }
            }

            curNode = curNode->Forward[0];

            if (curNode->Key != key) [[unlikely]]
            {
                found = false;
                return Head->Value;
            }

            found = true;

            return curNode->Value;
        }

        void AddNode(const K &key, const V &val) noexcept
        {
            std::array<SkipNode *, MaxLevel + 1> prev;

            SkipNode *curNode = Head;

            for (int curLevel = MaxLevel; curLevel >= 0; --curLevel)
            {
                while (curNode->Forward[curLevel]->Key <= key)
                {
                    __builtin_prefetch(
                        &curNode->Forward[curLevel]->Forward[curLevel]->Key);
                    curNode = curNode->Forward[curLevel];
                }

                prev[curLevel] = curNode;
            }

            if (prev[0]->Key == key) [[unlikely]]
            {
                return;
            }

            std::uint64_t level = nextLevel();
            SkipNode *__restrict newNode = NodesPool.acquire();

            if (!newNode) [[unlikely]]
            {
                std::cerr << "Couldn't get a pointer from mempool."
                          << std::endl;
                std::terminate();
                return;
            }

            newNode->Key = key;
            newNode->Value = val;
            newNode->Height = level;
            CurMax = std::max(CurMax, level);

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

        void DeleteNode(const K &key) noexcept
        {
            std::array<SkipNode *, MaxLevel + 1> prev;

            SkipNode *curNode = Head;

            for (int curLevel = MaxLevel; curLevel >= 0; --curLevel)
            {
                while (curNode->Forward[curLevel]->Key < key)
                {
                    __builtin_prefetch(
                        &curNode->Forward[curLevel]->Forward[curLevel]->Key);
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

                bool released = NodesPool.release(curNode);

                if (!released) [[unlikely]]
                {
                    std::terminate();
                }
            }
        }

        [[nodiscard]] V &GetHead(bool &found) noexcept
        {
            V &res = Head->Forward[0] != Tail ? Head->Forward[0]->Value
                                              : Head->Value;
            found = res.GetKey() != Head->Value.GetKey();

            return res;
        }
    };
} // namespace MarketExecution
