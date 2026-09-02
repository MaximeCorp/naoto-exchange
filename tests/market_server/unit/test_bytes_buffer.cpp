#include <gtest/gtest.h>
#include <bytes_buffer.hpp>

#include <cstring>

using naoto::BytesBuffer;

TEST(BytesBufferTest, StartsEmpty)
{
    BytesBuffer<16> buf;
    EXPECT_EQ(buf.GetSize(), 0u);
}

TEST(BytesBufferTest, CanAddReflectsRemainingCapacityWhenEmpty)
{
    BytesBuffer<8> buf;
    EXPECT_TRUE(buf.CanAdd(8));
    EXPECT_FALSE(buf.CanAdd(9));
}

TEST(BytesBufferTest, GetDataExposesWhatWasWritten)
{
    BytesBuffer<16> buf;
    const uint8_t payload[4] = {1, 2, 3, 4};

    buf.Add(payload, 4);
    EXPECT_EQ(std::memcmp(buf.GetData(), payload, 4), 0);
}

TEST(BytesBufferTest, ClearResetsSizeToZero)
{
    BytesBuffer<16> buf;
    const uint8_t payload[4] = {1, 2, 3, 4};
    buf.Add(payload, 4);

    buf.Clear();
    EXPECT_EQ(buf.GetSize(), 0u);
    EXPECT_TRUE(buf.CanAdd(16));
}

// Add() is the buffer's core contract: append `size` bytes and remember
// they're there, so a caller accumulating a message across multiple
// partial socket reads (OrderRouter's OrderBuffer/ConfirmationBuffer)
// ends up with the whole message, not a clobbered mess. This was
// previously a "LIKELY_BUG" test documenting that Add() silently failed
// this contract (Size never advanced); it's fixed now (Size += size),
// so this asserts the actually-expected behavior directly.
TEST(BytesBufferTest, AddAdvancesSize)
{
    BytesBuffer<16> buf;
    const uint8_t chunk[4] = {1, 2, 3, 4};

    buf.Add(chunk, 4);
    EXPECT_EQ(buf.GetSize(), 4u);
}

TEST(BytesBufferTest, SecondAddAppendsAfterTheFirst)
{
    BytesBuffer<16> buf;
    const uint8_t chunk1[4] = {1, 2, 3, 4};
    const uint8_t chunk2[4] = {9, 9, 9, 9};

    buf.Add(chunk1, 4);
    buf.Add(chunk2, 4);

    EXPECT_EQ(buf.GetSize(), 8u);

    uint8_t observed[8];
    std::memcpy(observed, buf.GetData(), 8);
    uint8_t expected[8] = {1, 2, 3, 4, 9, 9, 9, 9};
    EXPECT_EQ(std::memcmp(observed, expected, 8), 0)
        << "chunk2 should land right after chunk1, not overwrite it";
}

TEST(BytesBufferTest, CanAddAccountsForBytesAlreadyAdded)
{
    BytesBuffer<8> buf;
    const uint8_t chunk[5] = {1, 2, 3, 4, 5};

    buf.Add(chunk, 5);
    EXPECT_TRUE(buf.CanAdd(3));
    EXPECT_FALSE(buf.CanAdd(4));
}

// Read()/Shift() were previously untestable through the public API:
// with Add() not advancing Size, Size was permanently stuck at 0, so
// any Read() call underflowed Size (an unsigned size_t) to a huge value
// and then memmove()d that many bytes - a real out-of-bounds write, not
// just a logic bug, so we deliberately didn't call them at all. Now
// that Add() correctly tracks Size, they're both safe and worth
// covering directly.
TEST(BytesBufferTest, ReadConsumesFromTheFrontAndCompactsTheRemainder)
{
    BytesBuffer<16> buf;
    uint8_t whole[8] = {10, 11, 12, 13, 14, 15, 16, 17};
    buf.Add(whole, 8);

    uint8_t out[3];
    buf.Read(out, 3);

    uint8_t expectedOut[3] = {10, 11, 12};
    EXPECT_EQ(std::memcmp(out, expectedOut, 3), 0);
    EXPECT_EQ(buf.GetSize(), 5u);

    uint8_t remaining[5];
    std::memcpy(remaining, buf.GetData(), 5);
    uint8_t expectedRemaining[5] = {13, 14, 15, 16, 17};
    EXPECT_EQ(std::memcmp(remaining, expectedRemaining, 5), 0)
        << "Shift() should have compacted the unread bytes down to the "
           "front of the buffer";
}

TEST(BytesBufferTest, AddAfterPartialReadAppendsAfterTheCompactedRemainder)
{
    // Mirrors real usage: a partial message arrives, gets partially
    // consumed, and more bytes arrive afterward - the new bytes must
    // land after whatever's left, not overwrite it.
    BytesBuffer<16> buf;
    uint8_t first[4] = {1, 2, 3, 4};
    buf.Add(first, 4);

    uint8_t out[2];
    buf.Read(out, 2); // consumes {1, 2}, leaves {3, 4} at the front

    uint8_t second[2] = {9, 9};
    buf.Add(second, 2);

    EXPECT_EQ(buf.GetSize(), 4u);
    uint8_t observed[4];
    std::memcpy(observed, buf.GetData(), 4);
    uint8_t expected[4] = {3, 4, 9, 9};
    EXPECT_EQ(std::memcmp(observed, expected, 4), 0);
}
