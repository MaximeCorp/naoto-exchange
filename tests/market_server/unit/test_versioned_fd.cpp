#include <gtest/gtest.h>
#include <versioned_fd.hpp>

using naoto::VersionedFd;

TEST(VersionedFdTest, DefaultConstructedHasFdMinusOneAndGenZero)
{
    VersionedFd vfd;
    uint64_t val = vfd.load(std::memory_order_acquire);

    EXPECT_EQ(VersionedFd::Fd(val), -1);
    EXPECT_EQ(VersionedFd::Gen(val), 0u);
}

TEST(VersionedFdTest, ConstructedFromFdHasGenZero)
{
    VersionedFd vfd(42);
    uint64_t val = vfd.load(std::memory_order_acquire);

    EXPECT_EQ(VersionedFd::Fd(val), 42);
    EXPECT_EQ(VersionedFd::Gen(val), 0u);
}

TEST(VersionedFdTest, SwitchFdUpdatesFdAndIncrementsGen)
{
    VersionedFd vfd(5);

    vfd.SwitchFd(9);
    uint64_t val = vfd.load(std::memory_order_acquire);

    EXPECT_EQ(VersionedFd::Fd(val), 9);
    EXPECT_EQ(VersionedFd::Gen(val), 1u);
}

TEST(VersionedFdTest, RepeatedSwitchFdMonotonicallyIncrementsGen)
{
    VersionedFd vfd(1);

    vfd.SwitchFd(2);
    vfd.SwitchFd(3);
    vfd.SwitchFd(4);

    uint64_t val = vfd.load(std::memory_order_acquire);
    EXPECT_EQ(VersionedFd::Fd(val), 4);
    EXPECT_EQ(VersionedFd::Gen(val), 3u);
}

TEST(VersionedFdTest, SwitchFdToMinusOneRoundTrips)
{
    // -1 is used as the "no connection" sentinel throughout OrderRouter
    // (MatchingEngines[...]/AccountFd default to it, and get reset to it
    // on disconnect via SwitchFd(-1)); it needs to survive the pack/
    // unpack through the lower 32 bits intact.
    VersionedFd vfd(7);

    vfd.SwitchFd(-1);
    uint64_t val = vfd.load(std::memory_order_acquire);

    EXPECT_EQ(VersionedFd::Fd(val), -1);
    EXPECT_EQ(VersionedFd::Gen(val), 1u);
}

TEST(VersionedFdTest, FdAndGenAreIndependentlyExtractableFromASnapshot)
{
    // The whole point of packing fd+gen together is that a reader can
    // grab both atomically in one load() and later detect "did the fd
    // change under me" by comparing the packed value, without a lock.
    // Verify a snapshot decodes to both pieces correctly even after
    // several generations.
    VersionedFd vfd(100);
    vfd.SwitchFd(200);
    vfd.SwitchFd(300);

    uint64_t snapshot = vfd.load(std::memory_order_acquire);

    vfd.SwitchFd(400); // vfd itself moves on...

    // ...but the earlier snapshot still decodes to what it captured.
    EXPECT_EQ(VersionedFd::Fd(snapshot), 300);
    EXPECT_EQ(VersionedFd::Gen(snapshot), 2u);

    uint64_t latest = vfd.load(std::memory_order_acquire);
    EXPECT_EQ(VersionedFd::Fd(latest), 400);
    EXPECT_EQ(VersionedFd::Gen(latest), 3u);
    EXPECT_NE(latest, snapshot)
        << "a reader comparing snapshots must be able to detect the fd "
           "changed underneath it";
}
