// test_math.cpp — 数学工具单测（翻新计划 Phase 1）。用例覆盖 find_next_xy 全部分支与
// point_distance_from_segment 的垂足在段内/段外情况。
#include <gtest/gtest.h>

#include "core/MathUtil.h"
#include "core/Random.h"

namespace {

using lw::math::distance;
using lw::math::findNextXY;
using lw::math::getAngle;
using lw::math::getRandomAngle;
using lw::math::mixColor;
using lw::math::pointDistanceFromSegment;

constexpr double kApprox = 1e-9;

TEST(MathUtil, Distance) {
    EXPECT_NEAR(distance(0, 0, 3, 4), 5.0, kApprox);
    EXPECT_NEAR(distance(0, 0, 0, 0), 0.0, kApprox);
}

TEST(MathUtil, GetAngle) {
    EXPECT_NEAR(getAngle(0, 0, 1, 0), 0.0, kApprox);
    EXPECT_NEAR(getAngle(0, 0, 0, 1), lw::kPi / 2, kApprox);
    EXPECT_NEAR(getAngle(0, 0, -1, 0), lw::kPi, kApprox);
    EXPECT_NEAR(getAngle(0, 0, 0, -1), -lw::kPi / 2, kApprox);
}

TEST(MathUtil, MixColor) {
    // rate = color1 占比；白黑混合 0.5 → 灰。
    EXPECT_EQ(mixColor(0xFFFFFFFFu, 0x00000000u, 0.5), 0xFF7F7F7Fu);
    // 红黑混合 0.8 → (204,0,0)。
    EXPECT_EQ(mixColor(0xFFFF0000u, 0x00000000u, 0.8), 0xFFCC0000u);
}

TEST(MathUtil, RandomAngleRange) {
    lw::Rng rng(1);
    for (int i = 0; i < 100; ++i) {
        const double a = getRandomAngle(rng);
        EXPECT_GE(a, 0.0);
        EXPECT_LT(a, 2 * lw::kPi);
    }
}

// find_next_xy：水平向 +x，先撞右边界(x2)。
TEST(MathUtil, FindNextXY_Right) {
    lw::Rng rng(1);
    double x = 0.5, y = 0.5, angle = 0.0, rem = 10.0;
    const int res = findNextXY(x, y, angle, rem, rng);
    EXPECT_EQ(res, 2);
    EXPECT_NEAR(x, 1.0, kApprox);
    EXPECT_NEAR(y, 0.5, kApprox);
    EXPECT_NEAR(rem, 9.5, kApprox);
}

// find_next_xy：垂直向 +y，先撞上边界(y2)。
TEST(MathUtil, FindNextXY_Up) {
    lw::Rng rng(1);
    double x = 0.5, y = 0.5, angle = lw::kPi / 2, rem = 10.0;
    const int res = findNextXY(x, y, angle, rem, rng);
    EXPECT_EQ(res, 3);
    EXPECT_NEAR(x, 0.5, kApprox);
    EXPECT_NEAR(y, 1.0, kApprox);
    EXPECT_NEAR(rem, 9.5, kApprox);
}

// find_next_xy：向 -x，先撞左边界(x1)。
TEST(MathUtil, FindNextXY_Left) {
    lw::Rng rng(1);
    double x = 5.5, y = 5.5, angle = lw::kPi, rem = 1.0;
    const int res = findNextXY(x, y, angle, rem, rng);
    EXPECT_EQ(res, 0);
    EXPECT_NEAR(x, 5.0, kApprox);
    EXPECT_NEAR(y, 5.5, kApprox);
    EXPECT_NEAR(rem, 0.5, kApprox);
}

// find_next_xy：余长不足撞边界 → 走完本段（-2）。
TEST(MathUtil, FindNextXY_ConsumeRemaining) {
    lw::Rng rng(1);
    double x = 0.5, y = 0.5, angle = 0.0, rem = 0.3;
    const int res = findNextXY(x, y, angle, rem, rng);
    EXPECT_EQ(res, -2);
    EXPECT_NEAR(x, 0.8, kApprox);
    EXPECT_NEAR(y, 0.5, kApprox);
    EXPECT_NEAR(rem, 0.0, kApprox);
}

// find_next_xy：对角撞角（两向边界距离相等）→ -3，位置/余长不动，角度反向（+180°）。
TEST(MathUtil, FindNextXY_CornerTie) {
    lw::Rng rng(1);
    double x = 0.5, y = 0.5, angle = lw::kPi / 4, rem = 10.0;
    const int res = findNextXY(x, y, angle, rem, rng);
    EXPECT_EQ(res, -3);
    EXPECT_NEAR(x, 0.5, kApprox);
    EXPECT_NEAR(y, 0.5, kApprox);
    EXPECT_NEAR(rem, 10.0, kApprox);
    // 撞角改为反向 +180° + 微小偏置（确定性；2026-08 用户定夺，原版为随机角）。
    EXPECT_NEAR(angle, lw::kPi / 4 + lw::kPi + 4.0 * lw::kEps, kApprox);
}

// point_distance_from_segment：垂足在线段内。
TEST(MathUtil, PointDist_InSegment) {
    double ux, uy;
    const double d = pointDistanceFromSegment(5, 3, 0, 0, 0.0, 10.0, ux, uy);
    EXPECT_NEAR(d, 3.0, kApprox);
    EXPECT_NEAR(ux, 5.0, kApprox);
    EXPECT_NEAR(uy, 0.0, kApprox);
}

// point_distance_from_segment：点在端点外，取最近端点。
TEST(MathUtil, PointDist_BeyondEndpoint) {
    const double d = pointDistanceFromSegment(12, 0, 0, 0, 0.0, 10.0);
    EXPECT_NEAR(d, 2.0, kApprox);
}

// point_distance_from_segment：点在起点。
TEST(MathUtil, PointDist_AtStart) {
    const double d = pointDistanceFromSegment(0, 0, 0, 0, 0.0, 10.0);
    EXPECT_NEAR(d, 0.0, kApprox);
}

}  // namespace
