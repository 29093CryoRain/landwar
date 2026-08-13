// test_tint.cpp — 单色 tint 数学单测（统一"读图→tint→多版本"管线）。
#include <gtest/gtest.h>

#include "core/GameDefs.h"  // kSpecialUnitFaction / ArmyType
#include "render/TintMath.h"

namespace {

using lw::render::TintRgba;
using lw::render::tintPixel;

// ---- 整体 tint（grayscaleFirst=false，白源 → 目标色）----

TEST(TintMath, WhiteBecomesTargetColor) {
    const std::array<int, 3> orange{255, 127, 0};
    const TintRgba o = tintPixel(255, 255, 255, 255, orange, false, 0);
    EXPECT_EQ(o.r, 255);
    EXPECT_EQ(o.g, 127);
    EXPECT_EQ(o.b, 0);
    EXPECT_EQ(o.a, 255);
}

TEST(TintMath, BlackStaysBlack) {
    const TintRgba o = tintPixel(0, 0, 0, 255, std::array<int, 3>{255, 0, 0}, false, 0);
    EXPECT_EQ(o.r, 0);
    EXPECT_EQ(o.g, 0);
    EXPECT_EQ(o.b, 0);
}

TEST(TintMath, TransparentStaysTransparent) {
    const TintRgba o = tintPixel(200, 200, 200, 0, std::array<int, 3>{0, 255, 0}, false, 0);
    EXPECT_EQ(o.a, 0);
}

TEST(TintMath, AlphaPreserved) {
    const TintRgba o = tintPixel(255, 255, 255, 128, std::array<int, 3>{0, 255, 255}, false, 0);
    EXPECT_EQ(o.a, 128);
    EXPECT_EQ(o.g, 255);
}

// ---- 灰度化 tint（grayscaleFirst=true，彩色源 → 目标色×亮度）----

TEST(TintMath, GrayTintIsTargetTimesLuminance) {
    // 源 (255,0,0) → 亮度 76 → 红目标 (255,0,0) → (76,0,0)。
    const std::array<int, 3> red{255, 0, 0};
    const TintRgba o = tintPixel(255, 0, 0, 255, red, true, 0);
    EXPECT_EQ(o.r, 76);  // 0.299*255 ≈ 76
    EXPECT_EQ(o.g, 0);
    EXPECT_EQ(o.b, 0);
}

TEST(TintMath, GrayTintMapsHueToTarget) {
    // 绿色源 (0,255,0) 亮度 150 → 蓝目标 (0,85,255) → (0, 150*85/255, 150)。
    const TintRgba o = tintPixel(0, 255, 0, 255, std::array<int, 3>{0, 85, 255}, true, 0);
    EXPECT_EQ(o.r, 0);
    EXPECT_EQ(o.g, 50);  // 150*85/255
    EXPECT_EQ(o.b, 150);
}

// ---- 保留近白（preserveWhiteAbove）----

TEST(TintMath, PreserveWhiteKeepsCore) {
    // 亮度 255 ≥ 230 → 保留白（灰度源 → (255,255,255)）。
    const TintRgba o = tintPixel(255, 255, 255, 255, std::array<int, 3>{255, 0, 0}, true, 230);
    EXPECT_EQ(o.r, 255);
    EXPECT_EQ(o.g, 255);
    EXPECT_EQ(o.b, 255);
}

TEST(TintMath, PreserveWhiteTintsDimGlow) {
    // 亮度 76 < 230 → 照常 tint。
    const TintRgba o = tintPixel(255, 0, 0, 255, std::array<int, 3>{0, 255, 0}, true, 230);
    EXPECT_EQ(o.g, 76);
    EXPECT_EQ(o.r, 0);
}

TEST(TintMath, PreserveWhiteOffMeansTintAll) {
    // preserveWhiteAbove=0 → 白也 tint。
    const TintRgba o = tintPixel(255, 255, 255, 255, std::array<int, 3>{255, 0, 0}, true, 0);
    EXPECT_EQ(o.r, 255);
    EXPECT_EQ(o.g, 0);
}

// ---- 色相（hueOf）----

TEST(TintMath, HueOfBasic) {
    EXPECT_DOUBLE_EQ(lw::render::hueOf(255, 0, 0), 0.0);    // 红
    EXPECT_NEAR(lw::render::hueOf(255, 255, 0), 60.0, 0.01); // 黄
    EXPECT_NEAR(lw::render::hueOf(0, 255, 0), 120.0, 0.01);  // 绿
    EXPECT_NEAR(lw::render::hueOf(0, 255, 255), 180.0, 0.01);// 青
    EXPECT_NEAR(lw::render::hueOf(255, 0, 255), 300.0, 0.01);// 品
}

TEST(TintMath, HueOfGrayIsZero) {
    EXPECT_DOUBLE_EQ(lw::render::hueOf(128, 128, 128), 0.0);  // 灰 → 色相无关
}

// ---- 色相旋转（HueRotate，军队精灵用）----

TEST(TintMath, HueRotateRedToYellow) {
    // 红 (139,0,0) 色相 0° → +60° = 黄 (139,139,0)。
    const TintRgba o = lw::render::hueRotatePixel(139, 0, 0, 255, 60.0);
    EXPECT_EQ(o.r, 139);
    EXPECT_EQ(o.g, 139);
    EXPECT_EQ(o.b, 0);
    EXPECT_EQ(o.a, 255);
}

TEST(TintMath, HueRotateZeroIsIdentity) {
    const TintRgba o = lw::render::hueRotatePixel(139, 0, 0, 255, 0.0);
    EXPECT_EQ(o.r, 139);
    EXPECT_EQ(o.g, 0);
    EXPECT_EQ(o.b, 0);
}

TEST(TintMath, HueRotatePreservesShade) {
    // 去饱和阴影 (240,160,160) 色相 0° → +60° = (240,240,160)，明度/饱和度保留（不压暗）。
    const TintRgba o = lw::render::hueRotatePixel(240, 160, 160, 255, 60.0);
    EXPECT_EQ(o.r, 240);
    EXPECT_EQ(o.g, 240);
    EXPECT_EQ(o.b, 160);
}

TEST(TintMath, HueRotateKeepsWhiteCore) {
    // 白像素（s=0）色相旋转不变 → 激光白芯自动保留。
    const TintRgba o = lw::render::hueRotatePixel(255, 255, 255, 255, 120.0);
    EXPECT_EQ(o.r, 255);
    EXPECT_EQ(o.g, 255);
    EXPECT_EQ(o.b, 255);
}

TEST(TintMath, HueRotateTransparentStaysTransparent) {
    const TintRgba o = lw::render::hueRotatePixel(255, 0, 0, 0, 60.0);
    EXPECT_EQ(o.a, 0);
}

// ---- 特殊版归属表（签名兵种）----

TEST(TintMath, SpecialUnitFactionMapping) {
    // normal 无特殊；vanguard→青3、pioneer→蓝4、laser→绿5、bomb→橙6、mine→紫7。
    EXPECT_EQ(lw::kSpecialUnitFaction[static_cast<int>(lw::ArmyType::normal)], 0);
    EXPECT_EQ(lw::kSpecialUnitFaction[static_cast<int>(lw::ArmyType::vanguard)], 3);
    EXPECT_EQ(lw::kSpecialUnitFaction[static_cast<int>(lw::ArmyType::pioneer)], 4);
    EXPECT_EQ(lw::kSpecialUnitFaction[static_cast<int>(lw::ArmyType::laser)], 5);
    EXPECT_EQ(lw::kSpecialUnitFaction[static_cast<int>(lw::ArmyType::bomb)], 6);
    EXPECT_EQ(lw::kSpecialUnitFaction[static_cast<int>(lw::ArmyType::mine)], 7);
}

}  // namespace
