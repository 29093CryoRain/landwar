// test_tint.cpp — 单色 tint 数学单测（统一"读图→tint→多版本"管线）。
#include <gtest/gtest.h>

#include "core/GameDefs.h"  // ArmyType
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

// ---- 双色模板（视觉工程改进 ⑫：红势力成品渲染 → 反解 → 任意势力主副色重合成）----

using lw::render::TwoToneTemplate;
using lw::render::decomposeTwoTonePixel;
using lw::render::compositeTwoTone;

namespace {
// 逐通道断言 |got−want| ≤ tol（合成与反解有 Δ 量化/取整，允许小误差）。
void expectClose(const TintRgba& got, int r, int g, int b, int a, int tol = 2) {
    EXPECT_NEAR(got.r, r, tol);
    EXPECT_NEAR(got.g, g, tol);
    EXPECT_NEAR(got.b, b, tol);
    EXPECT_EQ(got.a, a);
}
}  // namespace

TEST(TintMath, TwoToneDecomposeBasic) {
    // (200,100,100)：v=200、m=100、灰（max==min? 否）→ Δ=hue(0° 红)=0。
    const TwoToneTemplate t = decomposeTwoTonePixel(200, 100, 100, 255);
    EXPECT_EQ(t.v, 200);
    EXPECT_EQ(t.m, 100);
    EXPECT_EQ(t.delta, 0);
    EXPECT_EQ(t.a, 255);
    // 橙 (255,200,0)：v=255、m=0、Δ=round(47.06°·255/360)=33。
    const TwoToneTemplate o = decomposeTwoTonePixel(255, 200, 0, 255);
    EXPECT_EQ(o.v, 255);
    EXPECT_EQ(o.m, 0);
    EXPECT_EQ(o.delta, 33);
    // 灰像素 → Δ=0。
    EXPECT_EQ(decomposeTwoTonePixel(128, 128, 128, 255).delta, 0);
    // 透明 → 全 0。
    const TwoToneTemplate tr = decomposeTwoTonePixel(255, 0, 0, 0);
    EXPECT_EQ(tr.v, 0);
    EXPECT_EQ(tr.a, 0);
}

TEST(TintMath, TwoToneRedFactionReconstruction) {
    // 合成(反解(p), 红, 浅灰191) ≡ p（红势力还原源图，±2/通道）。
    const std::array<int, 3> red{255, 0, 0};
    const std::array<int, 3> gray191{191, 191, 191};
    struct Px { int r, g, b, a; };
    const Px samples[] = {
        {255, 0, 0, 255},    // 纯红
        {200, 100, 100, 255},// 去饱和红
        {255, 200, 0, 255},  // 橙（非纯红色相）
        {255, 200, 200, 255},// 浅粉（高灰高白）
        {191, 191, 191, 255},// 基准浅灰
        {255, 255, 255, 255},// 白
        {0, 0, 0, 255},      // 黑
        {60, 60, 60, 255},   // 深灰
        {128, 64, 32, 255},  // 棕
        {255, 128, 64, 255}, // 橙红
        {255, 0, 0, 128},    // 半透明红
    };
    for (const auto& p : samples) {
        const TintRgba c = compositeTwoTone(decomposeTwoTonePixel(p.r, p.g, p.b, p.a), red, gray191);
        expectClose(c, p.r, p.g, p.b, p.a);
    }
}

TEST(TintMath, TwoToneEquivalentToHueRotateWithGraySecondary) {
    // 副色=浅灰191 时，新双色合成 ≡ 旧 HueRotate（新管线是旧管线的严格推广）。
    // 源基色 = 红（色相 0）→ 旧 shift = hue(Pc)；逐像素两路结果一致（±2/通道）。
    const std::array<int, 3> gray191{191, 191, 191};
    const std::array<std::array<int, 3>, 7> factions = {
        {{255, 0, 0}, {255, 255, 0}, {0, 255, 255}, {0, 85, 255},
         {0, 255, 0}, {255, 127, 0}, {255, 0, 255}}};
    struct Px { int r, g, b, a; };
    const Px samples[] = {
        {255, 0, 0, 255}, {200, 100, 100, 255}, {255, 200, 0, 255},
        {255, 200, 200, 255}, {191, 191, 191, 255}, {255, 255, 255, 255},
        {0, 0, 0, 255}, {128, 64, 32, 255}, {255, 128, 64, 255}, {60, 60, 60, 255},
    };
    for (const auto& pc : factions) {
        const double shift = lw::render::hueOf(pc[0], pc[1], pc[2]);
        for (const auto& p : samples) {
            const TintRgba neo =
                compositeTwoTone(decomposeTwoTonePixel(p.r, p.g, p.b, p.a), pc, gray191);
            const TintRgba old = lw::render::hueRotatePixel(p.r, p.g, p.b, p.a, shift);
            expectClose(neo, old.r, old.g, old.b, old.a);
        }
    }
}

TEST(TintMath, TwoToneGrayLevelBecomesSecondary) {
    // 基准灰 191 → 纯副色；白 → 纯白；黑 → 纯黑（与目标主色无关）。
    const std::array<int, 3> blue{0, 0, 255};
    const std::array<int, 3> sc{200, 50, 50};
    const TintRgba g = compositeTwoTone(decomposeTwoTonePixel(191, 191, 191, 255), blue, sc);
    expectClose(g, 200, 50, 50, 255, 0);  // 精确（无 Δ、无取整误差）
    const TintRgba w = compositeTwoTone(decomposeTwoTonePixel(255, 255, 255, 255), blue, sc);
    expectClose(w, 255, 255, 255, 255, 0);
    const TintRgba b = compositeTwoTone(decomposeTwoTonePixel(0, 0, 0, 255), blue, sc);
    expectClose(b, 0, 0, 0, 255, 0);
}

TEST(TintMath, TwoToneGrayBelowPivotMixesSecondaryBlack) {
    // (100,100,100)：g=100<191 → 副色比例 100/191，黑比例 91/191（黑贡献 0）。
    // 目标副色 (0,200,0)：100/191·200 = 104.7 → 105。
    const std::array<int, 3> red{255, 0, 0};
    const std::array<int, 3> sc{0, 200, 0};
    const TintRgba c = compositeTwoTone(decomposeTwoTonePixel(100, 100, 100, 255), red, sc);
    expectClose(c, 0, 105, 0, 255, 0);
}

TEST(TintMath, TwoToneHueOffsetPreservedForOtherFaction) {
    // 橙 (255,200,0) Δ≈46.6°（量化 33/255·360）→ 蓝势力结果色相 ≈ 240+46.6 ≈ 286.6°。
    const std::array<int, 3> blue{0, 0, 255};
    const std::array<int, 3> gray191{191, 191, 191};
    const TintRgba c = compositeTwoTone(decomposeTwoTonePixel(255, 200, 0, 255), blue, gray191);
    EXPECT_NEAR(lw::render::hueOf(c.r, c.g, c.b), 286.6, 2.0);
    // 与旧 HueRotate 一致（严格推广验证）：shift = 240°。
    const TintRgba old = lw::render::hueRotatePixel(255, 200, 0, 255, 240.0);
    expectClose(c, old.r, old.g, old.b, old.a);
}

TEST(TintMath, TwoToneTransparentStaysTransparent) {
    const TintRgba c = compositeTwoTone(decomposeTwoTonePixel(255, 0, 0, 0),
                                        std::array<int, 3>{0, 0, 255},
                                        std::array<int, 3>{191, 191, 191});
    EXPECT_EQ(c.a, 0);
}

}  // namespace
