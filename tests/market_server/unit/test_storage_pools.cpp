#include <gtest/gtest.h>
#include <readerwritercircularbuffer.h>
#include <single_threaded_storage_pool.hpp>
#include <storage_pool.hpp>

#include <atomic>
#include <set>
#include <thread>
#include <vector>

using naoto::SingleThreadedStoragePool;
using naoto::StoragePool;

// ---------------------------------------------------------------------
// SingleThreadedStoragePool
// ---------------------------------------------------------------------

TEST(SingleThreadedStoragePoolTest, ThrowsOnZeroCapacity)
{
    EXPECT_THROW(SingleThreadedStoragePool<int>(0), std::invalid_argument);
}

TEST(SingleThreadedStoragePoolTest, AcquireReturnsDistinctPointersUpToCapacity)
{
    SingleThreadedStoragePool<int> pool(4);

    std::set<int *> seen;
    for (int i = 0; i < 4; ++i)
    {
        int *p = pool.acquire();
        ASSERT_NE(p, nullptr);
        EXPECT_TRUE(seen.insert(p).second) << "pool handed out a duplicate pointer";
    }
}

TEST(SingleThreadedStoragePoolTest, AcquireBeyondCapacityReturnsNull)
{
    SingleThreadedStoragePool<int> pool(2);

    ASSERT_NE(pool.acquire(), nullptr);
    ASSERT_NE(pool.acquire(), nullptr);
    EXPECT_EQ(pool.acquire(), nullptr);
}

TEST(SingleThreadedStoragePoolTest, ReleasedObjectsCanBeReacquired)
{
    SingleThreadedStoragePool<int> pool(1);

    int *first = pool.acquire();
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(pool.acquire(), nullptr); // exhausted

    EXPECT_TRUE(pool.release(first));

    int *second = pool.acquire();
    EXPECT_EQ(second, first) << "the sole slot should be handed back out";
}

TEST(SingleThreadedStoragePoolTest, ReleaseNullptrFails)
{
    SingleThreadedStoragePool<int> pool(1);
    EXPECT_FALSE(pool.release(nullptr));
}

TEST(SingleThreadedStoragePoolTest, ReleaseBeyondCapacityFails)
{
    // Releasing more objects than the pool's capacity (e.g. a double
    // release, or releasing a foreign pointer) must be rejected rather
    // than silently growing the free list past Capacity.
    SingleThreadedStoragePool<int> pool(1);

    int *obj = pool.acquire();
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(pool.release(obj));
    EXPECT_FALSE(pool.release(obj)) << "double release must be rejected";
}

// ---------------------------------------------------------------------
// StoragePool (moodycamel-backed, multi-thread-capable)
// ---------------------------------------------------------------------

TEST(StoragePoolTest, ThrowsOnZeroCapacity)
{
    EXPECT_THROW(StoragePool<int> pool(0), std::invalid_argument);
}

TEST(StoragePoolTest, AcquireReturnsDistinctPointersUpToCapacity)
{
    StoragePool<int> pool(4);

    std::set<int *> seen;
    for (int i = 0; i < 4; ++i)
    {
        int *p = pool.acquire();
        ASSERT_NE(p, nullptr);
        EXPECT_TRUE(seen.insert(p).second);
    }

    EXPECT_EQ(pool.acquire(), nullptr);
}

TEST(StoragePoolTest, ReleaseAndReacquireRoundTrips)
{
    StoragePool<int> pool(1);

    int *obj = pool.acquire();
    ASSERT_NE(obj, nullptr);
    EXPECT_TRUE(pool.release(obj));

    int *again = pool.acquire();
    EXPECT_EQ(again, obj);
}

TEST(StoragePoolTest, LocalReleaseKeepsObjectOffTheSharedQueue)
{
    // localRelease() is the "producer keeps reusing its own recently
    // freed objects" fast path documented in the project notes. It must
    // make the object available to a subsequent acquire() on the same
    // thread without going through the moodycamel queue.
    StoragePool<int> pool(2);

    int *a = pool.acquire();
    int *b = pool.acquire();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    ASSERT_TRUE(pool.localRelease(a));

    int *reacquired = pool.acquire();
    EXPECT_EQ(reacquired, a) << "localRelease'd objects should be LIFO-reused first";
}

TEST(StoragePoolTest, LocalReleaseBeyondCapacityFails)
{
    StoragePool<int> pool(1);
    int *a = pool.acquire();
    ASSERT_NE(a, nullptr);

    ASSERT_TRUE(pool.localRelease(a));
    // LocalReuseBuffer is sized to Capacity; a second localRelease with
    // nothing acquired in between must not silently overflow it.
    EXPECT_FALSE(pool.localRelease(a));
}

TEST(StoragePoolTest, GetAvailableReflectsLocalAndSharedFreeObjects)
{
    StoragePool<int> pool(1);
    EXPECT_TRUE(pool.getAvailable());

    int *a = pool.acquire();
    ASSERT_NE(a, nullptr);
    EXPECT_FALSE(pool.getAvailable());

    ASSERT_TRUE(pool.release(a));
    EXPECT_TRUE(pool.getAvailable());
}

TEST(StoragePoolTest, ProducerConsumerAcrossThreads)
{
    // Mirrors the pool's intended SPSC usage: one thread only acquires
    // (dequeues from Free), a different thread only releases (enqueues
    // to Free), handed off over a plain SPSC channel of their own. Total
    // objects ever "in flight" between acquire and release must never
    // exceed Capacity, and every value written by the producer must be
    // observed intact by the consumer (no torn/reused-too-early slot).
    constexpr int kCapacity = 8;
    constexpr int kOps = 50000;
    StoragePool<int> pool(kCapacity);

    moodycamel::BlockingReaderWriterCircularBuffer<int *> handoff(kCapacity
                                                                   * 2);

    std::thread producer([&] {
        for (int i = 0; i < kOps; ++i)
        {
            int *p = nullptr;
            while ((p = pool.acquire()) == nullptr)
            {
                std::this_thread::yield();
            }
            *p = i;
            handoff.wait_enqueue(p);
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < kOps; ++i)
        {
            int *p = nullptr;
            handoff.wait_dequeue(p);
            EXPECT_EQ(*p, i) << "value written by producer must survive the handoff";
            ASSERT_TRUE(pool.release(p));
        }
    });

    producer.join();
    consumer.join();
}
