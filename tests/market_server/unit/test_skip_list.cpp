#include <gtest/gtest.h>
#include <skip_list.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

using naoto::SkipList;

namespace
{
    // SkipList<K, V, MaxLevel, Compare> requires V to be a pointer type.
    // We use int* so we can distinguish nodes by identity and by value.
    constexpr size_t kMaxLevel = 4;

    class SkipListTest : public ::testing::Test
    {
    protected:
        SkipList<int64_t, int *, kMaxLevel> list{64};
    };
} // namespace

TEST_F(SkipListTest, GetValOnEmptyListReportsNotFound)
{
    bool found = true;
    (void)list.GetVal(42, found);
    EXPECT_FALSE(found);
}

TEST_F(SkipListTest, AddThenGetValReturnsSameValue)
{
    int payload = 7;
    list.AddNode(10, &payload);

    bool found = false;
    int *out = list.GetVal(10, found);
    EXPECT_TRUE(found);
    EXPECT_EQ(out, &payload);
}

TEST_F(SkipListTest, GetValForMissingKeyReportsNotFound)
{
    int payload = 7;
    list.AddNode(10, &payload);

    bool found = true;
    (void)list.GetVal(11, found);
    EXPECT_FALSE(found);
}

TEST_F(SkipListTest, DuplicateKeyInsertIsIgnored)
{
    int first = 1;
    int second = 2;

    list.AddNode(5, &first);
    list.AddNode(5, &second); // same key, must not replace/duplicate

    bool found = false;
    int *out = list.GetVal(5, found);
    ASSERT_TRUE(found);
    EXPECT_EQ(out, &first) << "AddNode on an existing key must be a no-op";
}

TEST_F(SkipListTest, GetHeadReturnsSmallestKeyUnderDefaultLess)
{
    int a = 1, b = 2, c = 3;
    list.AddNode(30, &a);
    list.AddNode(10, &b); // smallest
    list.AddNode(20, &c);

    EXPECT_EQ(list.GetHead(), &b);
}

TEST_F(SkipListTest, GetHeadTracksSmallestAsNodesAreDeleted)
{
    int a = 1, b = 2, c = 3;
    list.AddNode(30, &a);
    list.AddNode(10, &b);
    list.AddNode(20, &c);

    ASSERT_EQ(list.GetHead(), &b);

    list.DeleteNode(10);
    EXPECT_EQ(list.GetHead(), &c);

    list.DeleteNode(20);
    EXPECT_EQ(list.GetHead(), &a);
}

TEST_F(SkipListTest, DeleteNodeThenGetValReportsNotFound)
{
    int a = 1;
    list.AddNode(10, &a);
    list.DeleteNode(10);

    bool found = true;
    (void)list.GetVal(10, found);
    EXPECT_FALSE(found);
}

TEST_F(SkipListTest, GetHeadOnEmptyListReturnsNullptr)
{
    // Head->Forward[0] is initialized to Tail, and Tail->Value is
    // nullptr, so GetHead() on an empty list should hand back nullptr
    // rather than a dangling/garbage pointer. This matters directly for
    // the order book: "no orders on this side" must be distinguishable
    // from "orders exist starting at address 0".
    EXPECT_EQ(list.GetHead(), nullptr);
}

TEST_F(SkipListTest, ReAddAfterDeleteSucceeds)
{
    // AddNode()'s duplicate-key guard compares against the immediate
    // predecessor's key; make sure a key that was inserted, deleted,
    // then re-inserted, is treated as a fresh insert rather than
    // silently ignored as though it were still present.
    int a = 1, b = 2;
    list.AddNode(10, &a);
    list.DeleteNode(10);
    list.AddNode(10, &b);

    bool found = false;
    int *out = list.GetVal(10, found);
    EXPECT_TRUE(found);
    EXPECT_EQ(out, &b);
    EXPECT_EQ(list.GetHead(), &b);
}

TEST_F(SkipListTest, DeleteNodeOnMissingKeyIsANoOp)
{
    int a = 1;
    list.AddNode(10, &a);

    EXPECT_NO_FATAL_FAILURE(list.DeleteNode(999));

    bool found = false;
    int *out = list.GetVal(10, found);
    EXPECT_TRUE(found);
    EXPECT_EQ(out, &a);
}

// Insert a decent number of random-ish keys and cross-check every lookup
// and the "head = min" invariant against std::set, which exercises the
// multi-level forward-pointer maintenance far more than a handful of
// hand-picked keys would.
TEST_F(SkipListTest, MatchesStdSetAcrossManyInsertsAndDeletes)
{
    SkipList<int64_t, int *, kMaxLevel> big{2048};
    std::vector<int> storage(1024);
    std::map<int64_t, int *> model; // key -> the pointer we inserted for it

    uint64_t state = 0x1234'5678'9abc'def0ULL;
    auto nextKey = [&state]() -> int64_t {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int64_t>((state >> 33) % 5000);
    };

    int nextSlot = 0;
    for (int i = 0; i < 1000 && nextSlot < static_cast<int>(storage.size());
         ++i)
    {
        int64_t key = nextKey();
        if (model.count(key))
        {
            continue;
        }
        int *slot = &storage[nextSlot++];
        model.emplace(key, slot);
        big.AddNode(key, slot);
    }

    ASSERT_FALSE(model.empty());
    ASSERT_NE(big.GetHead(), nullptr);
    EXPECT_EQ(big.GetHead(), model.begin()->second)
        << "GetHead() must return the value for the smallest key";

    for (auto &[key, ptr] : model)
    {
        bool found = false;
        int *out = big.GetVal(key, found);
        EXPECT_TRUE(found) << "key " << key << " should be present";
        EXPECT_EQ(out, ptr);
    }

    // Delete half the keys, then re-verify the remaining set is exactly
    // consistent (nothing else got dropped, nothing "reappears") and that
    // GetHead() still tracks the true minimum.
    std::vector<int64_t> allKeys;
    for (auto &[key, ptr] : model)
    {
        allKeys.push_back(key);
    }

    for (size_t i = 0; i < allKeys.size() / 2; ++i)
    {
        big.DeleteNode(allKeys[i]);
        model.erase(allKeys[i]);
    }

    ASSERT_FALSE(model.empty());
    EXPECT_EQ(big.GetHead(), model.begin()->second);

    for (int64_t key : allKeys)
    {
        bool found = false;
        (void)big.GetVal(key, found);
        EXPECT_EQ(found, model.count(key) == 1) << "key " << key;
    }
}

// The Bid side of the order book instantiates SkipList with std::greater
// so GetHead() returns the *highest* price. Verify that comparator
// actually flips ordering (this is a distinct code path: Head/Tail
// sentinels swap their min/max initialization based on Compare).
TEST(SkipListDescendingTest, GetHeadReturnsLargestKeyUnderGreater)
{
    SkipList<int64_t, int *, kMaxLevel, std::greater<int64_t>> list{64};
    int a = 1, b = 2, c = 3;

    list.AddNode(30, &a);
    list.AddNode(10, &b);
    list.AddNode(20, &c);

    EXPECT_EQ(list.GetHead(), &a); // 30 is the "best" (largest) under greater<>
}

TEST(SkipListDescendingTest, DeleteNodeTracksNewMaxUnderGreater)
{
    SkipList<int64_t, int *, kMaxLevel, std::greater<int64_t>> list{64};
    int a = 1, b = 2, c = 3;

    list.AddNode(30, &a);
    list.AddNode(10, &b);
    list.AddNode(20, &c);

    list.DeleteNode(30);
    EXPECT_EQ(list.GetHead(), &c); // 20 becomes the new max
}

TEST(SkipListDescendingTest, GetHeadOnEmptyListReturnsNullptr)
{
    SkipList<int64_t, int *, kMaxLevel, std::greater<int64_t>> list{64};
    EXPECT_EQ(list.GetHead(), nullptr);
}

// ---------------------------------------------------------------------
// Death tests: document current (crash-on-misuse) behavior at the two
// boundaries the skip list doesn't defend against. Neither of these is
// exercised by the "normal" tests above, but both are reachable if a
// caller isn't careful, so they're worth pinning down rather than
// leaving as silent assumptions.
// ---------------------------------------------------------------------

TEST(SkipListDeathTest, InsertingSentinelMaxKeyUnderLessCorruptsTraversal)
{
    // Under the default std::less<K>, Tail is seeded with
    // Key = numeric_limits<K>::max(). AddNode()'s scan condition is
    // `while (!comp(key, forward->Key))`, i.e. "keep advancing while
    // key >= forward->Key". If the caller inserts a key exactly equal
    // to numeric_limits<K>::max(), the scan advances *onto* Tail itself
    // (key == Tail->Key satisfies key >= Tail->Key) and then
    // dereferences Tail->Forward[level]->Key - but Tail->Forward
    // entries are all nullptr, so this is a null-pointer dereference.
    // In a real order book this would require a Price of INT64_MAX,
    // which should never happen, but there is no guard against it here
    // - flagging it as a hard boundary rather than an assumption.
    SkipList<int64_t, int *, kMaxLevel> list{16};
    int a = 1;
    constexpr int64_t kSentinelMax = std::numeric_limits<int64_t>::max();

    EXPECT_DEATH({ list.AddNode(kSentinelMax, &a); }, "");
}

TEST(SkipListDeathTest, InsertingSentinelMinKeyUnderGreaterCorruptsTraversal)
{
    // Mirror image of the above for the Bid side's std::greater<>
    // instantiation: there, Tail is seeded with
    // Key = numeric_limits<K>::min(), so a Price of INT64_MIN triggers
    // the same null dereference.
    SkipList<int64_t, int *, kMaxLevel, std::greater<int64_t>> list{16};
    int a = 1;
    constexpr int64_t kSentinelMin = std::numeric_limits<int64_t>::min();

    EXPECT_DEATH({ list.AddNode(kSentinelMin, &a); }, "");
}

TEST(SkipListDeathTest, AddNodeTerminatesWhenNodePoolIsExhausted)
{
    // Capacity is nodesPoolSize + 2 (two slots reserved for the Head
    // and Tail sentinels up front). With nodesPoolSize == 1, exactly
    // one real AddNode() can succeed; the next one finds the pool
    // empty and terminates rather than silently dropping the insert or
    // corrupting the list. Good to know this is a hard-fail, not a
    // no-op, since a matching engine hitting this in production would
    // otherwise drop a resting order without any signal.
    SkipList<int64_t, int *, kMaxLevel> list{1};
    int a = 1, b = 2;
    list.AddNode(10, &a);

    EXPECT_DEATH({ list.AddNode(20, &b); },
                 "Couldn't get a pointer from mempool");
}
