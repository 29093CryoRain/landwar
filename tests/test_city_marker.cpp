// test_city_marker.cpp — 玩家城市指示（P2 + 2026-08-07 修正 + 2026-08 箭头拆分）：
// 环随城市基建地块尺寸缩放（外圆直径 = max(w,h)+margin）；箭头**拆分四方向子图、固定
// 尺寸、位置随城市框移动**（不随城市大小缩放）。纯几何静态函数，无需 SDL 渲染器。
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

// 四箭头"尖端"位置（顺序 左/右/上/下；尖端在绘制矩形的指向侧边缘中点）。
struct ArrowTips {
    double lx, rx, ty, by;
};
ArrowTips tipsOf(const std::array<CityMarkerRenderer::ArrowRect, 4>& r) {
    return {r[0].cx + r[0].dstW / 2.0, r[1].cx - r[1].dstW / 2.0,
            r[2].cy + r[2].dstH / 2.0, r[3].cy - r[3].dstH / 2.0};
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

TEST(CityMarker, ArrowTipsAlignWithCityBoxEdges) {
    // 2026-08 箭头拆分：四箭头尖端对齐城市框四缘中点，外移 gap 格（markerArrowGap）。
    const CityMarkerRenderer::CityTarget t{10.0, 20.0, 2, 2};
    const auto r = CityMarkerRenderer::arrowRects(t, kCellPx, kGap);
    const double hw = static_cast<double>(t.w) * kCellPx / 2.0;  // 15
    const double hh = static_cast<double>(t.h) * kCellPx / 2.0;  // 15
    const double gapPx = kGap * kCellPx;                          // 9
    const auto tips = tipsOf(r);
    EXPECT_NEAR(tips.lx, t.cx - hw - gapPx, 0.6);
    EXPECT_NEAR(tips.rx, t.cx + hw + gapPx, 0.6);
    EXPECT_NEAR(tips.ty, t.cy - hh - gapPx, 0.6);
    EXPECT_NEAR(tips.by, t.cy + hh + gapPx, 0.6);
}

TEST(CityMarker, ArrowSizeConstantAcrossCitySizes) {
    // 核心改进：箭头尺寸**不随城市大小缩放**（1×1 与 3×3 箭头尺寸相同）；
    // 城市大小由位置吸收——大城四箭头外移（更靠框缘）。
    const CityMarkerRenderer::CityTarget small{0.0, 0.0, 1, 1};
    const CityMarkerRenderer::CityTarget big{0.0, 0.0, 3, 3};
    const auto rs = CityMarkerRenderer::arrowRects(small, kCellPx, kGap);
    const auto rb = CityMarkerRenderer::arrowRects(big, kCellPx, kGap);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(rs[i].dstW, rb[i].dstW) << "arrow " << i;
        EXPECT_EQ(rs[i].dstH, rb[i].dstH) << "arrow " << i;
    }
    const auto ts = tipsOf(rs);
    const auto tb = tipsOf(rb);
    EXPECT_LT(tb.lx, ts.lx);
    EXPECT_GT(tb.rx, ts.rx);
    EXPECT_LT(tb.ty, ts.ty);
    EXPECT_GT(tb.by, ts.by);
}

TEST(CityMarker, ArrowGapAdjustable) {
    // 间距可调：gap 大 → 箭头尖端离城市框更远。
    const CityMarkerRenderer::CityTarget t{0.0, 0.0, 1, 1};
    const auto r0 = tipsOf(CityMarkerRenderer::arrowRects(t, kCellPx, 0.0));
    const auto r1 = tipsOf(CityMarkerRenderer::arrowRects(t, kCellPx, 1.0));
    EXPECT_LT(r1.lx, r0.lx);
    EXPECT_GT(r1.rx, r0.rx);
    EXPECT_LT(r1.ty, r0.ty);
    EXPECT_GT(r1.by, r0.by);
}

TEST(CityMarker, ArrowSizesScaleWithZoom) {
    // 箭头尺寸只随 zoom（cellPx）线性缩放（lround 舍入 ≤2px 误差）；不随城市大小。
    const CityMarkerRenderer::CityTarget t{0.0, 0.0, 2, 2};
    const auto rLow = CityMarkerRenderer::arrowRects(t, 15.0, kGap);
    const auto rHigh = CityMarkerRenderer::arrowRects(t, 30.0, kGap);
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(rHigh[i].dstW, 2.0 * rLow[i].dstW, 2.0) << "arrow " << i;
        EXPECT_NEAR(rHigh[i].dstH, 2.0 * rLow[i].dstH, 2.0) << "arrow " << i;
    }
}

TEST(CityMarker, ArrowRectsOrderAndAspect) {
    // 顺序 = 左/右/上/下；源箭头"长 20 × 宽 30"（尖端在短边）→ 水平箭头（左/右）宽>长、
    // 垂直箭头（上/下）长>宽；绘制尺寸 dstW/dstH 即按此纵横比（21×32）。
    const CityMarkerRenderer::CityTarget t{0.0, 0.0, 1, 1};
    const auto r = CityMarkerRenderer::arrowRects(t, kCellPx, kGap);
    EXPECT_LT(r[0].dstW, r[0].dstH);  // 左箭头（水平，长 21 < 宽 32）
    EXPECT_LT(r[1].dstW, r[1].dstH);  // 右箭头（水平）
    EXPECT_GT(r[2].dstW, r[2].dstH);  // 上箭头（垂直，宽 32 > 长 21）
    EXPECT_GT(r[3].dstW, r[3].dstH);  // 下箭头（垂直）
    // 尖端侧：左箭头尖端在右缘、下箭头尖端在上缘（用 1×1 城验证，框缘 = 中心±7.5px）。
    const CityMarkerRenderer::CityTarget c{100.0, 200.0, 1, 1};
    const auto rc = CityMarkerRenderer::arrowRects(c, kCellPx, kGap);
    const double hw = kCellPx / 2.0 + kGap * kCellPx;  // 7.5 + 9 = 16.5
    EXPECT_NEAR(rc[0].cx + rc[0].dstW / 2.0, c.cx - hw, 0.6);  // 左箭头尖端（框左缘外）
    EXPECT_NEAR(rc[3].cy - rc[3].dstH / 2.0, c.cy + hw, 0.6);  // 下箭头尖端（框下缘外）
}

TEST(CityMarker, TargetDefaultsToSingleCellCenter) {
    // CityTarget 默认值：1×1 城、中心 (0,0)。draw 接收指针即可用（纯几何部分仅依赖尺寸函数）。
    const CityMarkerRenderer::CityTarget t;
    EXPECT_EQ(t.w, 1);
    EXPECT_EQ(t.h, 1);
    EXPECT_DOUBLE_EQ(t.cx, 0.0);
    EXPECT_DOUBLE_EQ(t.cy, 0.0);
}
