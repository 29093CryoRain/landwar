// test_city_render.cpp — P13 城市渲染纯几何单测（render/CityRenderer::compute，mock 相机/缩放）。
// 覆盖开发计划 P13 测试计划"渲染"项：高缩放细线围基建地块出现、低缩放图标取最小尺寸、
// 图标中心（=基建块几何中心）、等级→塔贴图映射（9 级表）、图标尺寸按 iconFitScale/1 倍兜底
// 适配、视野剔除。无需 SDL 渲染器（相机-only 构造 + setTowerSourceSize 模拟塔源纵横比）。
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "core/Config.h"
#include "render/Camera.h"
#include "render/CityRenderer.h"
#include "world/Map.h"

namespace {

using namespace lw;

// 相机 + 空地图 + 默认渲染常数（compute 走城市注册表，addCity 直接注册，无需 BMP）。
struct CityRenderFixture {
    Config cfg;
    Map map;
    render::Camera cam;
    Config::Render rc;
    render::CityRenderer renderer;

    CityRenderFixture()
        : cfg(Config::loadFromJson("{}")),
          renderer(cam) {  // 相机-only 构造（无 SDL 渲染器）
        map.configure(cfg.map);
        map.setCityConfig(cfg.city);
        cam.configure(math::ScreenTransform{15.0, 600, 95}, 2175, 1425, 105, 95);
        rc = cfg.render;
    }

    int addCity(int level, int baseX, int baseY) { return map.addCity(level, baseX, baseY); }
    render::CityRenderer::Frame compute() { return renderer.compute(map, rc); }
};

// 高缩放（zoom 2 → cellPx=30 > lineMinCellPx=20）：每城生成细线围区（外廓矩形 = 地块屏幕矩形）；
// 图标尺寸按 iconFitScale 适配（保留源纵横比，长/宽 ≤ 框 × 填充比，且再小一点点）。城放可见区
//（zoom 2 居中视野 ~world x∈[-4,69], y∈[24,71]）。
TEST(CityRender, HighZoomOutlineAndBoxFitIcon) {
    CityRenderFixture f;
    f.cam.zoomAt(2175.0 / 2, 1425.0 / 2, 2.0);
    ASSERT_GT(f.cam.cellPx(), f.rc.city.lineMinCellPx);  // 前提：确在高缩放档
    const int cid = f.addCity(4, 50, 50);           // 2×2 级4 城
    const City& c = f.map.city(cid);
    f.renderer.setTowerSourceSize(4, 100, 150);     // A = 高/宽 = 1.5（高塔）
    const double A4 = 150.0 / 100.0;
    // P1.2：square 也走 iconFitScale（值 = 框内可容纳最大宽 × 0.85，模拟旧 iconScale 行为）。
    f.rc.city.iconFitScale["square"][4] = std::min(static_cast<double>(c.w),
                                                   static_cast<double>(c.h) / A4) * 0.85;
    const auto frame = f.compute();

    ASSERT_EQ(frame.outlines.size(), 1u);
    // 外廓矩形 = 地块 (baseX..baseX+w, baseY..baseY+h) 的屏幕包围盒（y 翻转归一）。
    const int x0 = f.cam.toScreenXi(static_cast<double>(c.baseX));
    const int x1 = f.cam.toScreenXi(static_cast<double>(c.baseX + c.w));
    const int y0 = f.cam.toScreenYi(static_cast<double>(c.baseY));
    const int y1 = f.cam.toScreenYi(static_cast<double>(c.baseY + c.h));
    EXPECT_EQ(frame.outlines[0].x, std::min(x0, x1));
    EXPECT_EQ(frame.outlines[0].y, std::min(y0, y1));
    EXPECT_EQ(frame.outlines[0].w, std::abs(x1 - x0));
    EXPECT_EQ(frame.outlines[0].h, std::abs(y1 - y0));
    // 细线颜色 = 该城归属势力的加深色 → 外廓携带 colorIndex = 城市 ownerId。
    EXPECT_EQ(frame.outlines[0].colorIndex, c.ownerId);

    // 图标：iconFitScale 适配。框 = 2×2 格 = 60×60px；A=1.5 → 可容纳最大宽 fitW = min(60, 60/1.5)
    // = 40；iconFitScale 值 = 40/30 × 0.85 = 1.1333… → dstW = round(1.1333×30) = 34。
    ASSERT_EQ(frame.icons.size(), 1u);
    const auto& ic = frame.icons[0];
    EXPECT_EQ(ic.level, 4);
    const double cellPx = f.cam.cellPx();
    const double boxW = c.w * cellPx;
    const double boxH = c.h * cellPx;
    const double A = 150.0 / 100.0;
    const double fitW = std::min(boxW, boxH / A);
    const int expectedW = std::max(1, static_cast<int>(std::lround(
                                       f.rc.city.iconFitScale["square"][4] * cellPx)));
    const int expectedH = std::max(1, static_cast<int>(std::lround(expectedW * A)));
    EXPECT_EQ(ic.dstW, expectedW);
    EXPECT_EQ(ic.dstH, expectedH);
    // 长/宽都不超出基建地块框，且略小于框（0.85<1 → 不紧贴边缘）。
    EXPECT_LE(ic.dstW, boxW);
    EXPECT_LE(ic.dstH, boxH);
    EXPECT_LT(ic.dstW, fitW);  // 再小一点点
    // 保留源纵横比：dstH = round(dstW × A)。
    EXPECT_EQ(ic.dstH, std::max(1, static_cast<int>(std::lround(ic.dstW * A))));
    EXPECT_EQ(ic.cx, f.cam.toScreenXi(c.centerX()));
    EXPECT_EQ(ic.cy, f.cam.toScreenYi(c.centerY()));  // 中心 = 基建地块几何中心（无 offsetY）
    EXPECT_EQ(ic.colorIndex, c.ownerId);
    // 2026-08-07 反馈：城市不再画 data/city.png 基建格贴图，Frame 无 tiles 字段（只细线+图标）。
}

// 低缩放（zoom 0.25 → cellPx=3.75 < lineMinCellPx）：不画细线。
// **渲染改版（2026-08-08）**：普通城市随缩放继续缩小（无下限，P1.2 后按 1 倍兜底 < minIconSizePx）；
// 正式首都保底 minIconSizePx（不随缩放继续缩小）。未指定源纵横比 → 方形（dstH == dstW）。
TEST(CityRender, LowZoomNormalShrinksCapitalKeepsMinSize) {
    CityRenderFixture f;
    f.cam.zoomAt(2175.0 / 2, 1425.0 / 2, 0.25);
    ASSERT_LT(f.cam.cellPx(), f.rc.city.lineMinCellPx);  // 前提：确在低缩放档
    const int levels[5] = {1, 2, 4, 6, 9};
    for (int k = 0; k < 5; ++k) f.addCity(levels[k], 2 + k * 3, 2 + k * 3);
    const int capCity = f.addCity(4, 40, 40);  // 正式首都（2×2）
    std::vector<int> status(f.map.cityCount(), 0);
    status[static_cast<size_t>(capCity)] = 1;
    const auto frame = f.renderer.compute(f.map, f.rc, status);

    EXPECT_TRUE(frame.outlines.empty());  // 低缩放不画细线
    // 普通城市：随缩放缩小（无下限，dstW < 首都保底）；A=1 → dstH == dstW。
    ASSERT_EQ(frame.icons.size(), 5u);
    for (const auto& ic : frame.icons) {
        EXPECT_LT(ic.dstW, f.rc.capital.minIconSizePx) << "level " << ic.level;
        EXPECT_EQ(ic.dstH, ic.dstW) << "level " << ic.level;  // A=1（方形）
        EXPECT_GE(ic.dstW, 1);
    }
    // 正式首都：保底 render.capital.minIconSizePx（低缩放不继续缩小，比普通城市更显眼）。
    ASSERT_EQ(frame.capitalIcons.size(), 1u);
    EXPECT_EQ(frame.capitalIcons[0].dstW, f.rc.capital.minIconSizePx);
    EXPECT_EQ(frame.capitalIcons[0].dstH, f.rc.capital.minIconSizePx);  // A=1（未设首都源尺寸）
    // 各等级图标中心 = 城市基建块几何中心（无 offsetY），逐城校验。
    for (std::size_t i = 0; i < frame.icons.size(); ++i) {
        const City& c = f.map.city(static_cast<int>(i));
        EXPECT_EQ(frame.icons[i].cx, f.cam.toScreenXi(c.centerX())) << "city " << i;
        EXPECT_EQ(frame.icons[i].cy, f.cam.toScreenYi(c.centerY())) << "city " << i;
    }
}

// 等级 → 塔贴图映射（9 级塔表下标 = 等级-1，3/5/7/8 占位）+ 框适配：模拟各等级塔源纵横比
//（高塔→宽塔），高缩放下图标长/宽均 ≤ 基建地块框 × 填充比且保留源纵横比、略小于框。
TEST(CityRender, LevelsMapToTowersFitWithinBlock) {
    CityRenderFixture f;
    f.cam.zoomAt(2175.0 / 2, 1425.0 / 2, 2.0);  // cellPx=30，确保 fitW×0.85 ≥ min 不触下限
    const int levels[5] = {1, 2, 4, 6, 9};
    // 模拟塔源尺寸（源纵横比 高/宽）：1/2 高塔，4/6 中高，9 宽。
    const int sw[5] = {69, 69, 100, 150, 235};
    const int sh[5] = {145, 145, 150, 180, 187};
    for (int k = 0; k < 5; ++k) {
        f.renderer.setTowerSourceSize(levels[k], sw[k], sh[k]);
        const int cid = f.addCity(levels[k], k * 10, 30);  // 同一行铺开（zoom 2 居中视野 ~world x∈[-4,69]）
        const City& c = f.map.city(cid);
        const double A = static_cast<double>(sh[k]) / static_cast<double>(sw[k]);
        // P1.2：square 走 iconFitScale（模拟旧 iconScale=0.85 行为）。
        f.rc.city.iconFitScale["square"][levels[k]] =
            std::min(static_cast<double>(c.w), static_cast<double>(c.h) / A) * 0.85;
    }
    const auto frame = f.compute();
    ASSERT_EQ(frame.icons.size(), 5u);
    for (int k = 0; k < 5; ++k) {
        const auto& ic = frame.icons[static_cast<size_t>(k)];
        const City& c = f.map.city(k);
        const int lv = levels[k];
        EXPECT_EQ(ic.level, lv) << "k=" << k;
        const double A = static_cast<double>(sh[k]) / static_cast<double>(sw[k]);
        const double cellPx = f.cam.cellPx();
        const double boxW = static_cast<double>(c.w) * cellPx;
        const double boxH = static_cast<double>(c.h) * cellPx;
        const double fitW = std::min(boxW, boxH / A);
        const int expectedW = std::max(1,  // 普通城市无下限（2026-08-08）
                                       static_cast<int>(std::lround(
                                           f.rc.city.iconFitScale["square"][lv] * cellPx)));
        const int expectedH = std::max(1, static_cast<int>(std::lround(expectedW * A)));
        EXPECT_EQ(ic.dstW, expectedW) << "level " << lv;
        EXPECT_EQ(ic.dstH, expectedH) << "level " << lv;
        // 长/宽都不超出基建地块框；略小于框；保留源纵横比。
        EXPECT_LE(ic.dstW, boxW) << "level " << lv;
        EXPECT_LE(ic.dstH, boxH) << "level " << lv;
        EXPECT_LT(ic.dstW, fitW) << "level " << lv;
        EXPECT_EQ(ic.dstH, std::max(1, static_cast<int>(std::lround(ic.dstW * A)))) << "level " << lv;
    }
}

// P15 首都渲染：capitalStatus[c.id]（0 普通 / 1 正式首都 / 2 候补指定新都）分流。
// 正式首都 → 首都图标（capitalIcons）+ 更粗细线（capitalOutlines，不占普通 outlines）；
// 候补 → 虚化首都图标（designatedIcons）+ 普通细线；普通 → 塔图标 + 普通细线。
TEST(CityRender, CapitalAndDesignatedStatusRouting) {
    CityRenderFixture f;
    f.cam.zoomAt(2175.0 / 2, 1425.0 / 2, 2.0);  // 高缩放（细线档）
    const int cCap = f.addCity(2, 50, 50);
    const int cDes = f.addCity(4, 60, 30);  // zoom2 居中视野 ~x∈[-4,69] y∈[24,71]，须在内
    f.addCity(1, 30, 30);  // 普通城（cNormal：仅需存在，不引用）
    std::vector<int> status(f.map.cityCount(), 0);
    status[static_cast<size_t>(cCap)] = 1;
    status[static_cast<size_t>(cDes)] = 2;
    const auto frame = f.renderer.compute(f.map, f.rc, status);

    ASSERT_EQ(frame.icons.size(), 1u);                // 仅普通城市塔图标
    ASSERT_EQ(frame.capitalIcons.size(), 1u);         // 仅正式首都
    ASSERT_EQ(frame.designatedIcons.size(), 1u);      // 仅候补
    EXPECT_EQ(frame.icons[0].level, 1);
    EXPECT_EQ(frame.capitalIcons[0].level, 2);
    EXPECT_EQ(frame.designatedIcons[0].level, 4);
    ASSERT_EQ(frame.outlines.size(), 2u);             // 普通城 + 候补 → 普通细线
    ASSERT_EQ(frame.capitalOutlines.size(), 1u);      // 正式首都 → 更粗细线
}

// 首都图标尺寸按普通城图标面积 + 首都源纵横比（A = setCapitalSourceSize）反推。
// 普通城未配置 iconFitScale 时按 1 倍兜底。
TEST(CityRender, CapitalIconScalesByLevelAndSourceAspect) {
    CityRenderFixture f;
    f.cam.zoomAt(2175.0 / 2, 1425.0 / 2, 2.0);
    f.renderer.setCapitalSourceSize(100, 150);  // A = 高/宽 = 1.5
    const int cid = f.addCity(4, 50, 50);       // 2×2 级4 城
    std::vector<int> status(f.map.cityCount(), 0);
    status[static_cast<size_t>(cid)] = 1;
    const auto frame = f.renderer.compute(f.map, f.rc, status);
    ASSERT_EQ(frame.capitalIcons.size(), 1u);
    const auto& ic = frame.capitalIcons[0];
    const double cellPx = f.cam.cellPx();
    const double boxW = static_cast<double>(f.map.city(cid).w) * cellPx;
    const double boxH = static_cast<double>(f.map.city(cid).h) * cellPx;
    const double capA = 150.0 / 100.0;
    // 复刻 CityRenderer::compute 的首都面积反推（P15）：普通城图标面积 S → 按首都纵横比 capA
    // 反推宽高，保底 minIconSizePx。普通城 normalA = 塔源纵横比（本夹具未 setTowerSourceSize →
    // srcAspect=0 → 按 1.0）。
    const double normalA = 1.0;
    const double normalFitW = std::min(boxW, boxH / normalA);
    const int normalDstW = std::max(1, static_cast<int>(std::lround(normalFitW)));
    const int normalDstH = std::max(1, static_cast<int>(std::lround(normalDstW * normalA)));
    const double S = static_cast<double>(normalDstW) * static_cast<double>(normalDstH);
    const int expectedW =
        std::max(f.rc.capital.minIconSizePx, static_cast<int>(std::lround(std::sqrt(S / capA))));
    const int expectedH = std::max(1, static_cast<int>(std::lround(expectedW * capA)));
    EXPECT_EQ(ic.dstW, expectedW);
    EXPECT_EQ(ic.dstH, expectedH);
    EXPECT_GE(ic.dstW, f.rc.capital.minIconSizePx);
    EXPECT_LE(ic.dstW, boxW);
}

// 空 capitalStatus（向后兼容）→ 全部按普通城市处理（无首都/候补分流）。
TEST(CityRender, EmptyCapitalStatusDefaultsAllNormal) {
    CityRenderFixture f;
    f.cam.zoomAt(2175.0 / 2, 1425.0 / 2, 2.0);
    f.addCity(1, 30, 30);
    const auto frame = f.renderer.compute(f.map, f.rc);  // 缺省 status
    ASSERT_EQ(frame.icons.size(), 1u);
    EXPECT_TRUE(frame.capitalIcons.empty());
    EXPECT_TRUE(frame.designatedIcons.empty());
    ASSERT_EQ(frame.outlines.size(), 1u);
    EXPECT_TRUE(frame.capitalOutlines.empty());
}

// 视野剔除：zoom 1 整图可见 → 全部城市生成命令；zoom 4 居中 → 远离视口的城市被剔除。
TEST(CityRender, ViewCullingSkipsOffscreenCities) {
    CityRenderFixture f;
    // 分散放 9 城（覆盖全图 105×95）。
    const int xs[9] = {2, 50, 100, 2, 50, 100, 2, 50, 100};
    const int ys[9] = {2, 2, 2, 50, 50, 50, 90, 90, 90};
    for (int k = 0; k < 9; ++k) f.addCity(1, xs[k], ys[k]);
    const int total = f.map.cityCount();

    const auto all = f.compute();
    EXPECT_EQ(all.icons.size(), static_cast<std::size_t>(total));  // zoom 1 全部可见

    f.cam.zoomAt(2175.0 / 2, 1425.0 / 2, 4.0);  // 放大居中 → 视口小于全图
    const auto zoomed = f.compute();
    EXPECT_LT(zoomed.icons.size(), static_cast<std::size_t>(total));  // 至少剔除一些
    // 块级剔除保留"与视口相交"的块（含部分可见）→ 图标中心可贴近视口外不超过一个最大
    // 块跨度（最大形状 3×3）。被保留的图标中心须落在该放宽容差内。
    const int margin = static_cast<int>(3 * f.cam.cellPx());
    for (const auto& ic : zoomed.icons) {
        EXPECT_GE(ic.cx, -margin);
        EXPECT_LE(ic.cx, f.cam.windowWidth() + margin);
        EXPECT_GE(ic.cy, -margin);
        EXPECT_LE(ic.cy, f.cam.windowHeight() + margin);
    }
}

}  // namespace
