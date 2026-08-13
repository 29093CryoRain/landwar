// test_city_marker.cpp — 玩家城市指示尺寸（P2 + 2026-08-07 修正）：环/箭头随城市基建地块
// 尺寸与间距配置缩放（静态纯函数，无需 SDL 渲染器）。
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "render/CityMarkerRenderer.h"

namespace {

using lw::render::CityMarkerRenderer;

constexpr double kCellPx = 15.0;   // 默认 blockSize
constexpr double kMargin = 1.5;    // 默认 markerRingMargin
constexpr double kGap = 0.6;       // 默认 markerArrowGap

// 环外圆直径（格）≈ max(w,h)+margin（lround ≤0.5px 误差 → 0.5/cellPx 格）。
double ringOuterDiameterCells(int w, int h, double cellPx, double margin) {
    return CityMarkerRenderer::ringSizePx(w, h, cellPx, margin)
           * CityMarkerRenderer::kRingOuterFrac / cellPx;
}

// 箭头尖端半径（格）≈ max(w,h)/2+gap。
double arrowTipRadiusCells(int w, int h, double cellPx, double gap) {
    return CityMarkerRenderer::arrowSizePx(w, h, cellPx, gap)
           * CityMarkerRenderer::kArrowTipFrac / cellPx;
}

}  // namespace

TEST(CityMarker, RingDiameterMatchesCitySize) {
    // 环外圆直径 = max(w,h)+margin：1×1 → 2.5 格、3×3 → 4.5 格（小城环小、大城环大）。
    EXPECT_NEAR(ringOuterDiameterCells(1, 1, kCellPx, kMargin), 2.5, 0.05);
    EXPECT_NEAR(ringOuterDiameterCells(2, 2, kCellPx, kMargin), 3.5, 0.05);
    EXPECT_NEAR(ringOuterDiameterCells(3, 3, kCellPx, kMargin), 4.5, 0.05);
    // 非方形：取较大维。
    EXPECT_NEAR(ringOuterDiameterCells(2, 3, kCellPx, kMargin), 4.5, 0.05);
    EXPECT_NEAR(ringOuterDiameterCells(3, 2, kCellPx, kMargin), 4.5, 0.05);
}

TEST(CityMarker, RingMarginAdjustable) {
    // 自由缩放：margin 大 → 环大（且为线性）。
    const double d1 = ringOuterDiameterCells(1, 1, kCellPx, 0.5);
    const double d2 = ringOuterDiameterCells(1, 1, kCellPx, 2.0);
    EXPECT_NEAR(d1, 1.5, 0.05);
    EXPECT_NEAR(d2, 3.0, 0.05);
    EXPECT_GT(d2, d1);
}

TEST(CityMarker, RingEnclosesFullBlockDiagonal) {
    // 环外圆半径须 ≥ 对角一半 √(w²+h²)/2 才能圈住全部基建格（含角落）。
    // 默认 margin=1.5 对最大 3×3（√18/2≈2.12）成立。
    for (int w = 1; w <= 3; ++w) {
        for (int h = 1; h <= 3; ++h) {
            const double halfDiag = std::hypot(static_cast<double>(w),
                                               static_cast<double>(h)) / 2.0;
            const double outerR = ringOuterDiameterCells(w, h, kCellPx, kMargin) / 2.0;
            EXPECT_GE(outerR, halfDiag) << "w=" << w << " h=" << h;
        }
    }
}

TEST(CityMarker, ArrowTipRadiusMatchesCitySize) {
    // 尖端半径 = max(w,h)/2+gap：1×1 → 1.1 格、3×3 → 2.1 格（四箭头停在基建地块边缘外）。
    EXPECT_NEAR(arrowTipRadiusCells(1, 1, kCellPx, kGap), 1.1, 0.05);
    EXPECT_NEAR(arrowTipRadiusCells(2, 2, kCellPx, kGap), 1.6, 0.05);
    EXPECT_NEAR(arrowTipRadiusCells(3, 3, kCellPx, kGap), 2.1, 0.05);
    EXPECT_NEAR(arrowTipRadiusCells(2, 3, kCellPx, kGap), 2.1, 0.05);
}

TEST(CityMarker, ArrowGapAdjustable) {
    // 间距可调：gap 大 → 箭头离城更远（尺寸更大）。
    const double r1 = arrowTipRadiusCells(1, 1, kCellPx, 0.0);
    const double r2 = arrowTipRadiusCells(1, 1, kCellPx, 1.0);
    EXPECT_NEAR(r1, 0.5, 0.05);
    EXPECT_NEAR(r2, 1.5, 0.05);
    EXPECT_GT(r2, r1);
}

TEST(CityMarker, SizesScaleWithZoom) {
    // 与 cellPx 成正比：同一城在高缩放（cellPx 大）下指示更大（lround 舍入 ≤2px 误差）。
    const double szLow = CityMarkerRenderer::ringSizePx(2, 2, 15.0, kMargin);
    const double szHigh = CityMarkerRenderer::ringSizePx(2, 2, 30.0, kMargin);
    EXPECT_NEAR(szHigh, 2.0 * szLow, 2.0);
    const double aLow = CityMarkerRenderer::arrowSizePx(2, 2, 15.0, kGap);
    const double aHigh = CityMarkerRenderer::arrowSizePx(2, 2, 30.0, kGap);
    EXPECT_NEAR(aHigh, 2.0 * aLow, 2.0);
}

TEST(CityMarker, TargetDefaultsToSingleCellCenter) {
    // CityTarget 默认值：1×1 城、中心 (0,0)。draw 接收指针即可用（纯几何部分仅依赖尺寸函数）。
    const CityMarkerRenderer::CityTarget t;
    EXPECT_EQ(t.w, 1);
    EXPECT_EQ(t.h, 1);
    EXPECT_DOUBLE_EQ(t.cx, 0.0);
    EXPECT_DOUBLE_EQ(t.cy, 0.0);
}
