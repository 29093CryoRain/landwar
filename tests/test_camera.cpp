// test_camera.cpp — 相机坐标变换（Phase 8：缩放/平移/逆变换/无缝隙网格/夹取）。
#include <gtest/gtest.h>

#include "render/Camera.h"

namespace {

constexpr int kWinW = 2175, kWinH = 1425;
constexpr int kMapW = 105, kMapH = 95;

lw::render::Camera makeCam() {
    lw::render::Camera cam;
    cam.configure(lw::math::ScreenTransform{15.0, 600, 95}, kWinW, kWinH, kMapW, kMapH);
    return cam;
}

}  // namespace

TEST(Camera, IdentityMatchesScreenTransform) {
    const auto cam = makeCam();
    const lw::math::ScreenTransform tf{15.0, 600, 95};
    EXPECT_EQ(cam.zoom(), 1.0);
    EXPECT_DOUBLE_EQ(cam.toScreenX(10.0), tf.toX(10.0));
    EXPECT_DOUBLE_EQ(cam.toScreenY(40.0), tf.toY(40.0));
    EXPECT_DOUBLE_EQ(cam.cellPx(), 15.0);
}

TEST(Camera, RoundTripWorldScreenWorld) {
    auto cam = makeCam();
    cam.zoomAt(1200.0, 700.0, 1.7);
    cam.pan(80.0, -40.0);
    for (int i = 0; i < 20; ++i) {
        const double wx = 3.0 + i * 5.0;
        const double wy = 2.0 + i * 4.0;
        EXPECT_NEAR(cam.toWorldX(cam.toScreenX(wx)), wx, 1e-9);
        EXPECT_NEAR(cam.toWorldY(cam.toScreenY(wy)), wy, 1e-9);
    }
}

TEST(Camera, ZoomAtKeepsAnchorWorldFixed) {
    auto cam = makeCam();
    const double wx = cam.toWorldX(1200.0);
    const double wy = cam.toWorldY(700.0);
    cam.zoomAt(1200.0, 700.0, 2.0);
    EXPECT_NEAR(cam.toWorldX(1200.0), wx, 1e-9);  // 锚点下的世界坐标不变
    EXPECT_NEAR(cam.toWorldY(700.0), wy, 1e-9);
    EXPECT_NEAR(cam.zoom(), 2.0, 1e-9);
}

TEST(Camera, ZoomRangeClamped) {
    auto cam = makeCam();
    cam.zoomAt(1000.0, 600.0, 1e6);
    EXPECT_NEAR(cam.zoom(), lw::render::Camera::kMaxZoom, 1e-9);
    cam.zoomAt(1000.0, 600.0, 1e-12);
    EXPECT_NEAR(cam.zoom(), lw::render::Camera::kMinZoom, 1e-9);
}

TEST(Camera, PanClampedKeepsMapInView) {
    auto cam = makeCam();
    cam.pan(1e6, 1e6);
    EXPECT_GE(cam.toScreenX(kMapW), 0.0);  // 地图右缘仍 ≥ 0（未完全跑出左界）
    EXPECT_LE(cam.toScreenX(0.0), kWinW);  // 地图左缘 ≤ 视口右界
    EXPECT_GE(cam.toScreenY(0.0), 0.0);
    EXPECT_LE(cam.toScreenY(kMapH), kWinH);
    cam.pan(-1e6, -1e6);
    EXPECT_GE(cam.toScreenX(kMapW), 0.0);
    EXPECT_LE(cam.toScreenX(0.0), kWinW);
    EXPECT_GE(cam.toScreenY(0.0), 0.0);
    EXPECT_LE(cam.toScreenY(kMapH), kWinH);
}

TEST(Camera, LargeMapHighZoomPanReachesAllCorners) {
    // P6：随机图尺寸可与默认不同（如 200×200）；放大到最大后平移须能显示地图四角
    // （相机按实际地图尺寸配置时钳制范围正确；否则 mapW/mapH/tf_.mapHeight 按旧尺寸算
    // → 大图放大后够不到最边缘，2026-08-06 用户反馈）。
    constexpr int kW = 200, kH = 200;
    lw::render::Camera cam;
    cam.configure(lw::math::ScreenTransform{15.0, 600, kH}, kWinW, kWinH, kW, kH);
    cam.zoomAt(1000.0, 700.0, 4.0);  // 缩放到最大
    // 拖到左上极限 → 地图左上角世界 (0,kH) 可见（左缘/顶缘贴视口右/下界）。
    cam.pan(-1e6, 1e6);
    EXPECT_LE(cam.toScreenX(0), kWinW);
    EXPECT_LE(cam.toScreenY(kH), kWinH);
    // 拖到右下极限 → 地图右下角世界 (kW,0) 可见（右缘/底缘贴视口左/上界）。
    cam.pan(1e6, -1e6);
    EXPECT_GE(cam.toScreenX(kW), 0.0);
    EXPECT_GE(cam.toScreenY(0), 0.0);
}

TEST(Camera, FractionalCenterNotTruncated) {
    // 2026-08-07 回归：城市 1/2 级图标"略偏左"，且同一城缩小/放大后偏左程度不同。
    // 根因：ScreenTransform::toX/toY 在缩放前把世界坐标截断为 int → 非整数世界坐标
    //（城市中心 baseX+w/2）丢掉 0.5 格 → 图标比地块中心偏左/上 0.5×zoom px，随 zoom 变化。
    // 修复后 toScreenX/Y 走连续版 toXf/toYf：中点世界坐标的屏幕位置 = 两端整数坐标的中点。
    auto cam = makeCam();
    for (const double zf : {0.5, 1.0, 1.3, 2.0, 4.0}) {
        cam.reset();
        cam.zoomAt(1200.0, 700.0, zf);
        cam.pan(37.0, -21.0);
        const double mx = cam.toScreenX(10.5);
        const double midX = 0.5 * (cam.toScreenX(10.0) + cam.toScreenX(11.0));
        const double my = cam.toScreenY(40.5);
        const double midY = 0.5 * (cam.toScreenY(40.0) + cam.toScreenY(41.0));
        EXPECT_NEAR(mx, midX, 1e-6) << "zoom=" << zf;  // 旧 int 版偏差 0.5*zoom px，此处必然不通过
        EXPECT_NEAR(my, midY, 1e-6) << "zoom=" << zf;
    }
}

TEST(Camera, CellRectGaplessWhenZoomed) {
    auto cam = makeCam();
    cam.zoomAt(1000.0, 700.0, 1.3);
    cam.pan(120.0, -60.0);
    const auto r = cam.cellRect(5, 5);
    const auto rRight = cam.cellRect(6, 5);
    const auto rDown = cam.cellRect(5, 6);
    EXPECT_EQ(r.x + r.w, rRight.x);        // 水平无缝：右邻 x 起始 = 本格右缘
    EXPECT_EQ(r.y, rDown.y + rDown.h);     // 垂直无缝（y 翻转）：世界 j+1 格在屏幕更上方
    EXPECT_GT(r.w, 0);
    EXPECT_GT(r.h, 0);
}
