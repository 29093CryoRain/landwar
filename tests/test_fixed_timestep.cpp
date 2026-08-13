// test_fixed_timestep.cpp — FixedTimestep 累加器单测（2026-08 工程改进）。
// 用精确可表示的分数（0.5/1.5/2.0…）避免 1/60 浮点表示造成的脆弱断言。
// 注意：默认 maxAccum=0.25 是游戏参数（60Hz 步长下≈15 tick 上限）；步长为 1.0 的
// 语义测试须显式给足 maxAccum，否则会被默认截断（那不是类的问题）。
#include <gtest/gtest.h>

#include "core/FixedTimestep.h"

namespace {

using lw::FixedTimestep;

TEST(FixedTimestep, ConsumesExactlyOnePerStep) {
    FixedTimestep ts(1.0, 10.0);
    ts.advance(1.0);
    EXPECT_EQ(ts.consumeTicks(), 1);
    EXPECT_EQ(ts.consumeTicks(), 0);  // 余数不足一步
}

TEST(FixedTimestep, AccumulatesAcrossFrames) {
    FixedTimestep ts(1.0, 10.0);
    ts.advance(0.5);
    EXPECT_EQ(ts.consumeTicks(), 0);
    ts.advance(0.5);
    EXPECT_EQ(ts.consumeTicks(), 1);
}

TEST(FixedTimestep, RunsMultipleTicksInOneFrame) {
    FixedTimestep ts(1.0, 10.0);
    ts.advance(2.0);
    EXPECT_EQ(ts.consumeTicks(), 2);
}

TEST(FixedTimestep, SlowMotionSpawnsTicksEveryOtherFrame) {
    // 0.5x：两帧各 0.5 → 第一帧 0 tick，第二帧 1 tick（隔帧执行语义，与旧实现一致）。
    FixedTimestep ts(1.0, 10.0);
    ts.advance(0.5);
    EXPECT_EQ(ts.consumeTicks(), 0);
    ts.advance(0.5);
    EXPECT_EQ(ts.consumeTicks(), 1);
}

TEST(FixedTimestep, ClampsSpiralOfDeath) {
    // 单帧 100 >> maxAccum(2.5)：截断后至多 2 tick（防螺旋死亡，卡顿不掉帧追赶）。
    FixedTimestep ts(1.0, 2.5);
    ts.advance(100.0);
    EXPECT_EQ(ts.consumeTicks(), 2);
}

TEST(FixedTimestep, ResetClearsAccumulator) {
    FixedTimestep ts(1.0, 10.0);
    ts.advance(0.9);
    ts.reset();
    EXPECT_EQ(ts.consumeTicks(), 0);
}

TEST(FixedTimestep, RemainderCarriesOver) {
    FixedTimestep ts(1.0, 10.0);
    ts.advance(1.5);
    EXPECT_EQ(ts.consumeTicks(), 1);  // 余 0.5
    ts.advance(0.5);
    EXPECT_EQ(ts.consumeTicks(), 1);  // 0.5 + 0.5 = 1
}

TEST(FixedTimestep, GameRateStepIsConsumed) {
    // 与 Application::run 相同的 60Hz 参数（默认 maxAccum=0.25）：整帧 1/60s → 恰好 1 tick。
    FixedTimestep ts(1.0 / 60.0);
    ts.advance(1.0 / 60.0);
    EXPECT_EQ(ts.consumeTicks(), 1);
}

TEST(FixedTimestep, GameRateClampsAtQuarterSecond) {
    // 60Hz 下 1s 卡顿 → 截断到 0.25s → 至多 15 tick（Application 旧实现的防螺旋死亡语义）。
    FixedTimestep ts(1.0 / 60.0);
    ts.advance(1.0);
    EXPECT_EQ(ts.consumeTicks(), 15);
}

}  // namespace
