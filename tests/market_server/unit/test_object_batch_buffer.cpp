#include <gtest/gtest.h>
#include <object_batch.hpp>
#include <object_buffer.hpp>

#include <cstring>

using naoto::ObjectBatch;
using naoto::ObjectBuffer;

namespace
{
    // Prevents the compiler from constant-folding a literal `size`
    // argument all the way into the memcpy() inlined inside
    // ObjectBuffer::addBytes() below. Without this, GCC's fortified
    // memcpy bounds checker (-D_FORTIFY_SOURCE + -Werror=array-bounds,
    // which some distro/toolchain default flag sets enable) sees a
    // compile-time-provable "size larger than the destination" call
    // site and refuses to build - even though addBytes()'s own runtime
    // guard (`if (BufferSize + size > sizeof(T)) return false;`)
    // correctly rejects it before the memcpy ever executes. This is a
    // well-documented class of GCC false positive with fortified
    // memcpy + aggressive inlining, not a real bug in addBytes(). The
    // asm barrier forces `size` through a register, defeating constant
    // propagation, without changing runtime behavior at all.
    size_t OpaqueSize(size_t size) noexcept
    {
#if defined(__GNUC__)
        asm volatile("" : "+r"(size));
#endif
        return size;
    }
} // namespace

// --- ObjectBatch ----------------------------------------------------

TEST(ObjectBatchTest, DefaultsAreZero)
{
    ObjectBatch<int, 4> batch;
    EXPECT_EQ(batch.getSize(), 0u);
    EXPECT_EQ(batch.getFd(), 0u);
    EXPECT_EQ(batch.Auth, 0u);
}

TEST(ObjectBatchTest, SetSizeAndFdRoundTrip)
{
    ObjectBatch<int, 4> batch;
    batch.setSize(3);
    batch.setFd(77);

    EXPECT_EQ(batch.getSize(), 3u);
    EXPECT_EQ(batch.getFd(), 77u);
}

TEST(ObjectBatchTest, IndexOperatorAccessesUnderlyingStorage)
{
    ObjectBatch<int, 4> batch;
    batch[0] = 10;
    batch[1] = 20;

    EXPECT_EQ(batch[0], 10);
    EXPECT_EQ(batch[1], 20);
}

TEST(ObjectBatchTest, MoveConstructTransfersState)
{
    ObjectBatch<int, 4> batch;
    batch.setSize(2);
    batch.setFd(5);
    batch[0] = 111;

    ObjectBatch<int, 4> moved(std::move(batch));
    EXPECT_EQ(moved.getSize(), 2u);
    EXPECT_EQ(moved.getFd(), 5u);
    EXPECT_EQ(moved[0], 111);
}

// --- ObjectBuffer -----------------------------------------------------

namespace
{
    struct Fixed16
    {
        uint64_t a;
        uint64_t b;
    };
    static_assert(sizeof(Fixed16) == 16);
} // namespace

TEST(ObjectBufferTest, StartsEmpty)
{
    ObjectBuffer<Fixed16> buf;
    EXPECT_EQ(buf.BufferSize, 0u);
}

TEST(ObjectBufferTest, AddBytesAccumulatesUpToSizeofT)
{
    ObjectBuffer<Fixed16> buf;
    uint8_t chunk1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t chunk2[8] = {9, 10, 11, 12, 13, 14, 15, 16};

    EXPECT_TRUE(buf.addBytes(chunk1, 8));
    EXPECT_EQ(buf.BufferSize, 8u);
    EXPECT_TRUE(buf.addBytes(chunk2, 8));
    EXPECT_EQ(buf.BufferSize, 16u);

    uint8_t expected[16];
    std::memcpy(expected, chunk1, 8);
    std::memcpy(expected + 8, chunk2, 8);
    EXPECT_EQ(std::memcmp(buf.Buffer.data(), expected, 16), 0);
}

TEST(ObjectBufferTest, AddBytesRejectsOverflow)
{
    ObjectBuffer<Fixed16> buf;
    uint8_t chunk[17] = {0};

    EXPECT_FALSE(buf.addBytes(chunk, OpaqueSize(17)));
    EXPECT_EQ(buf.BufferSize, 0u) << "a rejected add must not partially write";
}

TEST(ObjectBufferTest, AddBytesRejectsWhenCombinedSizeExceedsSizeofT)
{
    ObjectBuffer<Fixed16> buf;
    uint8_t chunk[10] = {0};

    ASSERT_TRUE(buf.addBytes(chunk, OpaqueSize(10)));
    EXPECT_FALSE(buf.addBytes(chunk, OpaqueSize(10))); // 10 + 10 > 16
    EXPECT_EQ(buf.BufferSize, 10u) << "the failed add must not corrupt state";
}

TEST(ObjectBufferTest, ReadBytesConsumesFromFrontAndCompactsRemainder)
{
    ObjectBuffer<Fixed16> buf;
    uint8_t whole[16];
    for (int i = 0; i < 16; ++i)
    {
        whole[i] = static_cast<uint8_t>(i);
    }
    ASSERT_TRUE(buf.addBytes(whole, 16));

    uint8_t out[8];
    buf.readBytes(out, 8);

    uint8_t expectedOut[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    EXPECT_EQ(std::memcmp(out, expectedOut, 8), 0);
    EXPECT_EQ(buf.BufferSize, 8u);

    uint8_t remaining[8];
    std::memcpy(remaining, buf.Buffer.data(), 8);
    uint8_t expectedRemaining[8] = {8, 9, 10, 11, 12, 13, 14, 15};
    EXPECT_EQ(std::memcmp(remaining, expectedRemaining, 8), 0);
}

TEST(ObjectBufferTest, ClearBufferResetsSize)
{
    ObjectBuffer<Fixed16> buf;
    uint8_t chunk[4] = {1, 2, 3, 4};
    ASSERT_TRUE(buf.addBytes(chunk, 4));

    buf.clearBuffer();
    EXPECT_EQ(buf.BufferSize, 0u);
    EXPECT_TRUE(buf.addBytes(chunk, 4)); // full capacity available again
}
