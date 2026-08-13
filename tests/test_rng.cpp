// test_rng.cpp — 确定性随机数单测（翻新计划 §3.6）。
#include <gtest/gtest.h>

#include "core/Random.h"

namespace {

TEST(Rng, SameSeedSameSequence) {
    lw::Rng a(29093);
    lw::Rng b(29093);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(a.get(1000), b.get(1000));
    }
}

TEST(Rng, GetRange) {
    lw::Rng rng(7);
    for (int i = 0; i < 2000; ++i) {
        const int v = rng.get(97);
        EXPECT_GE(v, 0);
        EXPECT_LE(v, 97);
    }
    // 单点域：[0,0] 恒为 0。
    EXPECT_EQ(rng.get(0), 0);
}

TEST(Rng, Chance) {
    lw::Rng rng(3);
    EXPECT_TRUE(rng.chance(1.0));
    EXPECT_FALSE(rng.chance(0.0));
    // 0.5 上下各抽若干次，应出现 true 与 false（不要求精确分布）。
    bool sawTrue = false, sawFalse = false;
    for (int i = 0; i < 200; ++i) {
        if (rng.chance(0.5))
            sawTrue = true;
        else
            sawFalse = true;
    }
    EXPECT_TRUE(sawTrue);
    EXPECT_TRUE(sawFalse);
}

TEST(Rng, StateRoundTrip) {
    lw::Rng a(7);
    a.get(100);
    a.get(100);
    const std::string state = a.state();

    lw::Rng b(999);  // 不同种子
    b.setState(state);

    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(a.get(1000), b.get(1000));
    }
}

TEST(Rng, SeedValue) {
    lw::Rng rng(12345);
    EXPECT_EQ(rng.seedValue(), 12345u);
}

TEST(Rng, UnitAndRange) {
    // 现代化补充 API：unit() ∈ [0,1)，range() ∈ [lo,hi)。
    lw::Rng rng(5);
    for (int i = 0; i < 200; ++i) {
        const double u = rng.unit();
        EXPECT_GE(u, 0.0);
        EXPECT_LT(u, 1.0);
        const double r = rng.range(-2.0, 3.0);
        EXPECT_GE(r, -2.0);
        EXPECT_LT(r, 3.0);
    }
}

TEST(Rng, RandomSeedIsCallable) {
    // 现代种子（时间驱动）：非确定性来源。仅验证可重复调用、不抛异常。
    for (int i = 0; i < 10; ++i) {
        const std::uint32_t s = lw::Rng::randomSeed();
        (void)s;
    }
}

}  // namespace
