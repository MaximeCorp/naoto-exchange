#include <gtest/gtest.h>
#include <sequence_ring_buffer.hpp>

using naoto::SequenceRingBuffer;

namespace
{
    struct Slot
    {
        uint32_t SequenceId;
        int Payload;
    };
} // namespace

TEST(SequenceRingBufferTest, GetSlotOnEmptyBufferReturnsNull)
{
    SequenceRingBuffer<Slot, 8> buf;
    EXPECT_EQ(buf.GetSlot(0), nullptr);
}

TEST(SequenceRingBufferTest, AddThenGetRoundTrips)
{
    SequenceRingBuffer<Slot, 8> buf;
    Slot s{3, 42};

    EXPECT_TRUE(buf.AddSlot(&s));
    EXPECT_EQ(buf.GetSlot(3), &s);
}

TEST(SequenceRingBufferTest, GetSlotForWrongSequenceIdReturnsNull)
{
    // Same buffer index (mod Size) but different sequence id must not be
    // returned as a false positive - this is the whole point of storing
    // SequenceId alongside the slot.
    SequenceRingBuffer<Slot, 8> buf;
    Slot s{3, 42};
    ASSERT_TRUE(buf.AddSlot(&s));

    EXPECT_EQ(buf.GetSlot(3 + 8), nullptr); // same idx, different seq id
}

TEST(SequenceRingBufferTest, HandlesOutOfOrderArrivalWithinWindow)
{
    // DPDK receive thread can see reordering/gaps; sequence ids don't
    // have to arrive monotonically as long as they're within the window.
    SequenceRingBuffer<Slot, 8> buf;
    Slot s0{0, 0}, s1{1, 1}, s2{2, 2};

    ASSERT_TRUE(buf.AddSlot(&s2));
    ASSERT_TRUE(buf.AddSlot(&s0));
    ASSERT_TRUE(buf.AddSlot(&s1));

    EXPECT_EQ(buf.GetSlot(0), &s0);
    EXPECT_EQ(buf.GetSlot(1), &s1);
    EXPECT_EQ(buf.GetSlot(2), &s2);
}

TEST(SequenceRingBufferTest, LaterSlotOverwritesSameIndexEarlierOne)
{
    SequenceRingBuffer<Slot, 8> buf;
    Slot s0{0, 100};
    Slot s8{8, 800}; // same idx as seq 0 (8 & 7 == 0)

    ASSERT_TRUE(buf.AddSlot(&s0));
    ASSERT_TRUE(buf.AddSlot(&s8));

    EXPECT_EQ(buf.GetSlot(8), &s8);
    EXPECT_EQ(buf.GetSlot(0), nullptr)
        << "slot 0 was overwritten by slot 8 at the same ring index";
}

TEST(SequenceRingBufferTest, RejectsSequenceIdTooFarBehindTail)
{
    constexpr size_t Size = 8;
    SequenceRingBuffer<Slot, Size> buf;

    // Advance Tail well past Size by adding a high sequence id.
    Slot high{100, 1};
    ASSERT_TRUE(buf.AddSlot(&high)); // Tail becomes 101

    // Anything with sequenceId < Tail - Size (i.e. < 93) must be rejected
    // as stale/out-of-window, rather than silently accepted and
    // corrupting the ring.
    Slot stale{50, 2};
    EXPECT_FALSE(buf.AddSlot(&stale));
}

TEST(SequenceRingBufferTest, AcceptsSequenceIdAtWindowBoundary)
{
    constexpr size_t Size = 8;
    SequenceRingBuffer<Slot, Size> buf;

    Slot high{100, 1};
    ASSERT_TRUE(buf.AddSlot(&high)); // Tail becomes 101

    // Tail - Size == 93: this is the oldest id still considered valid.
    Slot atBoundary{93, 2};
    EXPECT_TRUE(buf.AddSlot(&atBoundary));
}

TEST(SequenceRingBufferTest, TailAdvancesOnlyForwards)
{
    SequenceRingBuffer<Slot, 8> buf;
    Slot low{2, 1};
    Slot lower{1, 2};

    ASSERT_TRUE(buf.AddSlot(&low));  // Tail -> 3
    ASSERT_TRUE(buf.AddSlot(&lower)); // seq 1 < Tail(3), still within window -> accepted, Tail unaffected

    EXPECT_EQ(buf.GetSlot(1), &lower);
    EXPECT_EQ(buf.GetSlot(2), &low);
}
