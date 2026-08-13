// test_wrapdraw.cpp — P10 边界贯通（环绕）双渲染辅助纯逻辑单测。
// collectWrapDraws：兵距图边 < edgeDist 时收集主位置 + 对侧副本（含角上对角）屏幕坐标。
#include <gtest/gtest.h>

#include "render/WrapDraw.h"

namespace {

using namespace lw::render;

// 场景：10×10 图，主位置屏幕 (100,100)，图跨度 spanX=spanY=150，edgeDist=2 格。
// 世界 y=0 在屏幕底 → 下副本 = sy - spanY、上副本 = sy + spanY。

TEST(WrapDraw, CenterOnlyPrimary) {
    int out[9][2];
    const int n = collectWrapDraws(5.0, 5.0, 2.0, 10.0, 10.0, 100, 100, 150, 150, out);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(out[0][0], 100);
    EXPECT_EQ(out[0][1], 100);
}

TEST(WrapDraw, NearLeftAddsRightCopy) {
    int out[9][2];
    const int n = collectWrapDraws(0.5, 5.0, 2.0, 10.0, 10.0, 100, 100, 150, 150, out);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(out[1][0], 100 + 150);  // 右副本 = +spanX
    EXPECT_EQ(out[1][1], 100);
}

TEST(WrapDraw, NearRightAddsLeftCopy) {
    int out[9][2];
    const int n = collectWrapDraws(9.5, 5.0, 2.0, 10.0, 10.0, 100, 100, 150, 150, out);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(out[1][0], 100 - 150);  // 左副本 = -spanX
    EXPECT_EQ(out[1][1], 100);
}

TEST(WrapDraw, NearBottomAddsTopCopy) {
    int out[9][2];
    const int n = collectWrapDraws(5.0, 0.5, 2.0, 10.0, 10.0, 100, 100, 150, 150, out);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(out[1][0], 100);
    EXPECT_EQ(out[1][1], 100 - 150);  // 上副本（世界 y+height）= sy - spanY
}

TEST(WrapDraw, NearTopAddsBottomCopy) {
    int out[9][2];
    const int n = collectWrapDraws(5.0, 9.5, 2.0, 10.0, 10.0, 100, 100, 150, 150, out);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(out[1][0], 100);
    EXPECT_EQ(out[1][1], 100 + 150);  // 下副本（世界 y-height）= sy + spanY
}

TEST(WrapDraw, BottomLeftCornerFourCopies) {
    int out[9][2];
    // 左下角：主 + 右 + 上 + 右上对角 = 4。
    const int n = collectWrapDraws(0.5, 0.5, 2.0, 10.0, 10.0, 100, 100, 150, 150, out);
    ASSERT_EQ(n, 4);
    EXPECT_EQ(out[1][0], 250);
    EXPECT_EQ(out[1][1], 100);
    EXPECT_EQ(out[2][0], 100);
    EXPECT_EQ(out[2][1], -50);
    EXPECT_EQ(out[3][0], 250);  // 对角（右上）
    EXPECT_EQ(out[3][1], -50);
}

TEST(WrapDraw, NotNearEdgeNoCopies) {
    // 贴界阈值小到只有贴边才算；中间格不画副本。
    int out[9][2];
    const int n = collectWrapDraws(1.0, 5.0, 0.5, 10.0, 10.0, 100, 100, 150, 150, out);
    EXPECT_EQ(n, 1);
}

}  // namespace
