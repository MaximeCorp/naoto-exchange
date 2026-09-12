#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <single_threaded_storage_pool.hpp>
#include <x86intrin.h>

namespace naoto
{
    template <std::integral K, typename V, size_t PoolSize, size_t MaxLevel,
              typename Compare = std::less<K>>
        requires std::is_pointer_v<V>
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
        Compare comp;
        SkipNode *Head;
        SkipNode *Tail;
        SingleThreadedStoragePool<SkipNode, PoolSize> NodesPool;
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
        // Capacity is the PoolSize template parameter (the pool is a fixed
        // std::array); the head and the tail each take one of those slots.
        SkipList(void)
            : Head(nullptr)
            , Tail(nullptr)
            , State(__rdtsc())
            , CurMax(0)
        {
            Tail = NodesPool.Acquire();

            if constexpr (std::is_same_v<Compare, std::less<K>>)
            {
                Tail->Key = std::numeric_limits<K>::max();
            }
            else
            {
                Tail->Key = std::numeric_limits<K>::min();
            }

            Tail->Value = nullptr;
            Tail->Height = MaxLevel;
            Tail->Forward.fill(nullptr);

            Head = NodesPool.Acquire();

            if constexpr (std::is_same_v<Compare, std::less<K>>)
            {
                Head->Key = std::numeric_limits<K>::min();
            }
            else
            {
                Head->Key = std::numeric_limits<K>::max();
            }

            Head->Value = nullptr;
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
                bool released = NodesPool.Release(toRelease);

                if (!released) [[unlikely]]
                {
                    std::cerr << "skip list\n";
                    std::terminate();
                }
            }
        }

        [[nodiscard]] V &GetVal(const K &key, bool &found) noexcept
        {
            SkipNode *curNode = Head;

            for (int curLevel = MaxLevel; curLevel >= 0; --curLevel)
            {
                while (comp(curNode->Forward[curLevel]->Key, key))
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
                while (!comp(key, curNode->Forward[curLevel]->Key))
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
            SkipNode *__restrict newNode = NodesPool.Acquire();

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
                while (comp(curNode->Forward[curLevel]->Key, key))
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

                bool released = NodesPool.Release(curNode);

                if (!released) [[unlikely]]
                {
                    std::cerr << "skiplist2\n";
                    std::terminate();
                }
            }
        }

        [[nodiscard]] V GetHead() noexcept
        {
            return Head->Forward[0]->Value;
        }
    };
} // namespace naoto
