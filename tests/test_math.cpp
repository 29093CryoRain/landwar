// test_math.cpp — 数学工具单测（翻新计划 Phase 1）。用例覆盖
// point_distance_from_segment 的垂足在段内/段外情况。
#include <gtest/gtest.h>

#include <cmath>

#include "core/MathUtil.h"
#include "core/Random.h"

namespace {

using lw::math::distance;
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
    // L5：rate 越界时通道 clamp 到 [0,255]，不溢出污染相邻通道。
    EXPECT_EQ(mixColor(0xFFFFFFFFu, 0x00000000u, 2.0), 0xFFFFFFFFu);   // 255*2 → 255
    EXPECT_EQ(mixColor(0x00000000u, 0xFFFFFFFFu, 2.0), 0xFF000000u);   // 255*(1-2) → 0
    EXPECT_EQ(mixColor(0xFFFFFFFFu, 0x00000000u, -1.0), 0xFF000000u);  // 255*(-1) → 0
    // NaN → 逐通道 0（截断前已防护，不产生 UB）。
    EXPECT_EQ(mixColor(0xFFFFFFFFu, 0xFFFFFFFFu, std::nan("")), 0xFF000000u);
}

TEST(MathUtil, RandomAngleRange) {
    lw::Rng rng(1);
    for (int i = 0; i < 100; ++i) {
        const double a = getRandomAngle(rng);
        EXPECT_GE(a, 0.0);
        EXPECT_LT(a, 2 * lw::kPi);
    }
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
