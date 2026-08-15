// test_city_icon_fitter.cpp — CityIconFitter 几何工具单测。
// 覆盖：单格正方形/矩形、两格并集、L 形并集、三角形（凸多边形并集边界判定）。
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "world/tiling/CityIconFitter.h"

using namespace lw;

namespace {

FitPoly rect(double x0, double y0, double x1, double y1) {
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// 等边三角形（上尖，底边水平），中心 (cx,cy)，边长 side。
FitPoly triUp(double cx, double cy, double side) {
    const double h = side * 0.8660254037844386;
    return {{cx - side / 2.0, cy - h / 3.0},
            {cx + side / 2.0, cy - h / 3.0},
            {cx, cy + 2.0 * h / 3.0}};
}

TEST(CityIconFitter, SingleSquareUnit) {
    std::vector<FitPoly> cells = {rect(0.0, 0.0, 1.0, 1.0)};
    double scale = 0.0, cx = 0.0, cy = 0.0;
    ASSERT_TRUE(CityIconFitter::compute(cells, 1.0, 1.0, scale, cx, cy));
    EXPECT_NEAR(scale, 1.0, 1e-6);
    EXPECT_NEAR(cx, 0.5, 1e-6);
    EXPECT_NEAR(cy, 0.5, 1e-6);
}

TEST(CityIconFitter, SingleRectangleAspectKeepsInside) {
    // 2x1 矩形放 1x1 方形贴图 → 最大边长 = 1。
    std::vector<FitPoly> cells = {rect(0.0, 0.0, 2.0, 1.0)};
    double scale = 0.0, cx = 0.0, cy = 0.0;
    ASSERT_TRUE(CityIconFitter::compute(cells, 1.0, 1.0, scale, cx, cy));
    EXPECT_NEAR(scale, 1.0, 1e-5);
    EXPECT_TRUE(CityIconFitter::rectangleInside(cells, cx, cy, 1.0, 1.0, scale));
    EXPECT_FALSE(CityIconFitter::rectangleInside(cells, cx, cy, 1.0, 1.0, scale * 1.01));
}

TEST(CityIconFitter, TwoAdjacentSquares) {
    // 1x2 并集放 1x1 贴图 → 最大边长 1（可平移）。
    std::vector<FitPoly> cells = {rect(0.0, 0.0, 1.0, 1.0), rect(0.0, 1.0, 1.0, 2.0)};
    double scale = 0.0, cx = 0.0, cy = 0.0;
    ASSERT_TRUE(CityIconFitter::compute(cells, 1.0, 1.0, scale, cx, cy));
    EXPECT_NEAR(scale, 1.0, 1e-5);
    EXPECT_TRUE(CityIconFitter::rectangleInside(cells, cx, cy, 1.0, 1.0, scale));
}

TEST(CityIconFitter, LShapeSquareTexture) {
    // 三个单位格拼成 2x2 缺右上角的 L 形，方形贴图最大边长 1（放哪根臂都行）。
    std::vector<FitPoly> cells = {rect(0.0, 0.0, 1.0, 1.0), rect(1.0, 0.0, 2.0, 1.0),
                                  rect(0.0, 1.0, 1.0, 2.0)};
    double scale = 0.0, cx = 0.0, cy = 0.0;
    ASSERT_TRUE(CityIconFitter::compute(cells, 1.0, 1.0, scale, cx, cy));
    EXPECT_NEAR(scale, 1.0, 1e-4);
    EXPECT_TRUE(CityIconFitter::rectangleInside(cells, cx, cy, 1.0, 1.0, scale));
    EXPECT_FALSE(CityIconFitter::rectangleInside(cells, cx, cy, 1.0, 1.0, scale * 1.05));
}

TEST(CityIconFitter, TriangleSquareTextureFitsInside) {
    // 单三角形：工具应给出不越界的缩放（无需具体值；验证贴图矩形确实在三角形内）。
    std::vector<FitPoly> cells = {triUp(1.0, 1.0, 2.0)};
    double scale = 0.0, cx = 0.0, cy = 0.0;
    ASSERT_TRUE(CityIconFitter::compute(cells, 1.0, 1.0, scale, cx, cy));
    EXPECT_GT(scale, 0.1);
    EXPECT_TRUE(CityIconFitter::rectangleInside(cells, cx, cy, 1.0, 1.0, scale));
    EXPECT_FALSE(CityIconFitter::rectangleInside(cells, cx, cy, 1.0, 1.0, scale * 1.5));
}

TEST(CityIconFitter, DegenerateInputs) {
    std::vector<FitPoly> empty;
    double scale = -1.0, cx = -1.0, cy = -1.0;
    EXPECT_FALSE(CityIconFitter::compute(empty, 1.0, 1.0, scale, cx, cy));
    EXPECT_FALSE(CityIconFitter::compute({rect(0, 0, 1, 1)}, 0.0, 1.0, scale, cx, cy));
    EXPECT_FALSE(CityIconFitter::compute({rect(0, 0, 1, 1)}, 1.0, 0.0, scale, cx, cy));
}

}  // namespace
