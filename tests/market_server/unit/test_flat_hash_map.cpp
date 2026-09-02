#include <gtest/gtest.h>
#include <flat_hash_map.hpp>

#include <cstdint>
#include <map>
#include <random>
#include <vector>

using naoto::FlatHashMap;

TEST(FlatHashMapTest, GetOnEmptyMapReturnsFalse)
{
    FlatHashMap<int64_t, int, 8> map;
    int out = 0;
    EXPECT_FALSE(map.GetVal(1, out));
}

TEST(FlatHashMapTest, AddThenGetRoundTrips)
{
    FlatHashMap<int64_t, int, 8> map;
    map.AddNode(1, 111);

    int out = 0;
    ASSERT_TRUE(map.GetVal(1, out));
    EXPECT_EQ(out, 111);
}

TEST(FlatHashMapTest, GetForAbsentKeyReturnsFalse)
{
    FlatHashMap<int64_t, int, 8> map;
    map.AddNode(1, 111);

    int out = 0;
    EXPECT_FALSE(map.GetVal(2, out));
}

TEST(FlatHashMapTest, AddNodeOnExistingKeyDoesNotOverwrite)
{
    FlatHashMap<int64_t, int, 8> map;
    map.AddNode(1, 111);
    map.AddNode(1, 222); // AddNode silently no-ops when the key exists

    int out = 0;
    ASSERT_TRUE(map.GetVal(1, out));
    EXPECT_EQ(out, 111);
}

TEST(FlatHashMapTest, DeleteRemovesKey)
{
    FlatHashMap<int64_t, int, 8> map;
    map.AddNode(1, 111);
    map.DeleteNode(1);

    int out = 0;
    EXPECT_FALSE(map.GetVal(1, out));
}

TEST(FlatHashMapTest, DeleteOnAbsentKeyIsANoOp)
{
    FlatHashMap<int64_t, int, 8> map;
    map.AddNode(1, 111);

    EXPECT_NO_FATAL_FAILURE(map.DeleteNode(999));

    int out = 0;
    ASSERT_TRUE(map.GetVal(1, out));
    EXPECT_EQ(out, 111);
}

TEST(FlatHashMapTest, MultipleKeysAllRetrievable)
{
    FlatHashMap<int64_t, int, 16> map;

    for (int64_t k = 0; k < 50; ++k)
    {
        map.AddNode(k, static_cast<int>(k * 10));
    }

    for (int64_t k = 0; k < 50; ++k)
    {
        int out = -1;
        ASSERT_TRUE(map.GetVal(k, out)) << "missing key " << k;
        EXPECT_EQ(out, k * 10);
    }
}

// Keys that collide into the same home bucket exercise Robin Hood probing
// (dib comparisons, PaddingSafeSwap) rather than the empty-slot fast path.
TEST(FlatHashMapTest, CollidingKeysAllRetrievableAndDeletable)
{
    constexpr size_t Size = 4; // 4 buckets * 16 slots = 64 slots total
    FlatHashMap<int64_t, int, Size> map;

    // hash_64(key) = key * golden_ratio_constant; home bucket = (hash &
    // (Size-1)) << 4. Multiples of Size collide into the same home bucket
    // under this hash (since (k*Size*C) & (Size-1) == 0 for all k).
    std::vector<int64_t> keys;
    for (int64_t k = 0; k < 12; ++k)
    {
        keys.push_back(k * static_cast<int64_t>(Size));
    }

    for (size_t i = 0; i < keys.size(); ++i)
    {
        map.AddNode(keys[i], static_cast<int>(i));
    }

    for (size_t i = 0; i < keys.size(); ++i)
    {
        int out = -1;
        ASSERT_TRUE(map.GetVal(keys[i], out)) << "missing colliding key " << keys[i];
        EXPECT_EQ(out, static_cast<int>(i));
    }

    // Delete one from the middle of the probe chain and make sure the
    // backward-shift deletion doesn't disturb its neighbours.
    map.DeleteNode(keys[5]);

    int out = -1;
    EXPECT_FALSE(map.GetVal(keys[5], out));

    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (i == 5)
        {
            continue;
        }
        out = -1;
        ASSERT_TRUE(map.GetVal(keys[i], out))
            << "key " << keys[i] << " should survive neighbour deletion";
        EXPECT_EQ(out, static_cast<int>(i));
    }
}

// Insertions that probe past the end of the slot array must wrap around;
// the FootPrints/Tags/Keys/Data arrays keep a mirrored copy of the first
// 16 slots specifically so a 16-byte SIMD load starting near the end of
// the table doesn't need a branch. Exercise that wraparound directly by
// forcing keys into the last bucket.
TEST(FlatHashMapTest, WraparoundProbingWorks)
{
    constexpr size_t Size = 4; // total_size = 64, last bucket starts at 48
    FlatHashMap<int64_t, int, Size> map;

    // Fill the last bucket (idx 48..63) up with 16 colliding keys so the
    // 17th, 18th... must probe past idx 63 and wrap to idx 0.
    std::vector<int64_t> keys;
    int64_t k = 3; // home bucket = (k & (Size-1)) << 4 = 3<<4 = 48 (last bucket)
    for (int i = 0; i < 20; ++i)
    {
        while (((k * 0x9E3779B97F4A7C15ULL) & (Size - 1)) != 3)
        {
            ++k;
        }
        keys.push_back(k);
        ++k;
    }

    for (size_t i = 0; i < keys.size(); ++i)
    {
        map.AddNode(keys[i], static_cast<int>(i));
    }

    for (size_t i = 0; i < keys.size(); ++i)
    {
        int out = -1;
        ASSERT_TRUE(map.GetVal(keys[i], out))
            << "key " << keys[i] << " should be found after wraparound insert";
        EXPECT_EQ(out, static_cast<int>(i));
    }
}

// Cross-check against std::map with randomized insert/delete/lookup
// sequences.
TEST(FlatHashMapTest, MatchesStdMapUnderRandomOps)
{
    constexpr size_t Size = 32; // 512 slots, keep load factor low enough
                                 // to always find a slot
    FlatHashMap<int64_t, int64_t, Size> map;
    std::map<int64_t, int64_t> model;

    std::mt19937_64 rng(0xC0FFEE);
    std::uniform_int_distribution<int64_t> keyDist(0, 300);
    std::uniform_int_distribution<int> opDist(0, 2);

    for (int iter = 0; iter < 5000; ++iter)
    {
        int64_t key = keyDist(rng);
        int op = opDist(rng);

        if (op == 0) // insert
        {
            if (!model.count(key))
            {
                model.emplace(key, key * 1000);
                map.AddNode(key, key * 1000);
            }
        }
        else if (op == 1) // delete
        {
            map.DeleteNode(key);
            model.erase(key);
        }
        else // lookup
        {
            int64_t out = -1;
            bool found = map.GetVal(key, out);
            bool shouldFind = model.count(key) != 0;
            ASSERT_EQ(found, shouldFind) << "key " << key << " at iter " << iter;
            if (shouldFind)
            {
                EXPECT_EQ(out, model.at(key)) << "key " << key << " at iter " << iter;
            }
        }
    }

    for (auto &[key, val] : model)
    {
        int64_t out = -1;
        ASSERT_TRUE(map.GetVal(key, out)) << "final check, key " << key;
        EXPECT_EQ(out, val);
    }
}
