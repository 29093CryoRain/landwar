// test_tiling.cpp — 密铺纯几何单测（P12；开发计划 §8.1 测试先行）。
// 只链 landwar_core，无 SDL/无 RNG：中心往返、邻接对称、环绕一致性、
// 三角形顶/底平、穿越求交（含顶点平局确定性）、城市形状表（格数=等级、连通）。
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/Config.h"
#include "core/GameDefs.h"
#include "world/tiling/Tiling.h"

namespace {

using lw::TilingGeom;
using lw::TilingType;
using lw::kEps;
using lw::kPi;

// 邻接对称：neighbor(idx,k) == n（界内）时，存在某 k' 使 neighbor(n,k') == idx。
TEST(Tiling, CenterRoundTripAllTilings) {
    const TilingGeom cases[] = {{TilingType::Square, 11, 7},
                                {TilingType::Hex, 10, 8},
                                {TilingType::Hex, 13, 6},
                                {TilingType::Tri, 10, 8},
                                {TilingType::Tri, 9, 6}};
    for (const auto& g : cases) {
        for (int idx = 0; idx < g.cellCount(); ++idx) {
            double wx, wy;
            g.cellCenter(idx, wx, wy);
            EXPECT_EQ(g.worldToCell(wx, wy), idx) << "tiling=" << static_cast<int>(g.type)
                                                  << " idx=" << idx;
        }
    }
}

TEST(Tiling, WorldToCellResolvesDenseCellInteriors) {
    const TilingGeom cases[] = {{TilingType::Square, 9, 7},
                                {TilingType::Hex, 9, 7},
                                {TilingType::Tri, 9, 7},
                                {TilingType::Arch33336, 9, 7},
                                {TilingType::Laves33336, 9, 7}};
    for (const auto& g : cases) {
        for (int index = 0; index < g.cellCount(); ++index) {
            double cx = 0.0, cy = 0.0, vx[12], vy[12];
            g.cellCenter(index, cx, cy);
            const int n = g.cellPolygon(index, vx, vy, 12);
            ASSERT_GE(n, 3);
            for (int vertex = 0; vertex < n; ++vertex) {
                for (double amount : {0.15, 0.35, 0.65, 0.85}) {
                    const double x = cx + amount * (vx[vertex] - cx);
                    const double y = cy + amount * (vy[vertex] - cy);
                    EXPECT_EQ(g.worldToCell(x, y), index)
                        << "tiling=" << static_cast<int>(g.type) << " index=" << index
                        << " vertex=" << vertex << " amount=" << amount;
                }
                const int next = (vertex + 1) % n;
                const double x = 0.2 * cx + 0.4 * vx[vertex] + 0.4 * vx[next];
                const double y = 0.2 * cy + 0.4 * vy[vertex] + 0.4 * vy[next];
                EXPECT_EQ(g.worldToCell(x, y), index)
                    << "tiling=" << static_cast<int>(g.type) << " index=" << index
                    << " edge=" << vertex;
            }
        }
        EXPECT_EQ(g.worldToCell(-1e-3, 0.5 * g.worldHeight()), -1);
        EXPECT_EQ(g.worldToCell(g.worldWidth() + 1e-3, 0.5 * g.worldHeight()), -1);
    }
}

TEST(Tiling, RectRowRangeCoversLocatedCellsAndNarrowsSkewedPeriods) {
    const TilingGeom cases[] = {{TilingType::Square, 9, 7},
                                {TilingType::Hex, 9, 7},
                                {TilingType::Tri, 9, 7},
                                {TilingType::Arch33336, 9, 20},
                                {TilingType::Laves33336, 9, 20}};
    for (const auto& g : cases) {
        for (int index = 0; index < g.cellCount(); ++index) {
            double cx = 0.0, cy = 0.0, vx[12], vy[12];
            g.cellCenter(index, cx, cy);
            const int n = g.cellPolygon(index, vx, vy, 12);
            ASSERT_GE(n, 3);
            int row = -1, col = -1, base = -1;
            g.indexToRowCol(index, row, col, base);
            for (int sample = -1; sample < n; ++sample) {
                const double x = sample < 0 ? cx : cx + 0.8 * (vx[sample] - cx);
                const double y = sample < 0 ? cy : cy + 0.8 * (vy[sample] - cy);
                ASSERT_EQ(g.worldToCell(x, y), index);
                int r0 = 0, r1 = -1, c0 = 0, c1 = -1;
                g.rowRange(x - 0.01, y - 0.01, x + 0.01, y + 0.01, r0, r1);
                ASSERT_GE(row, r0)
                    << "tiling=" << static_cast<int>(g.type) << " index=" << index;
                ASSERT_LE(row, r1)
                    << "tiling=" << static_cast<int>(g.type) << " index=" << index;
                g.colRange(x - 0.01, x + 0.01, row, c0, c1);
                EXPECT_GE(col, c0)
                    << "tiling=" << static_cast<int>(g.type) << " index=" << index;
                EXPECT_LE(col, c1)
                    << "tiling=" << static_cast<int>(g.type) << " index=" << index;
            }
        }
        if (g.type == TilingType::Arch33336 || g.type == TilingType::Laves33336) {
            double x = 0.0, y = 0.0;
            g.cellCenter(g.cellIndexAt(g.rows / 2, g.cols / 2, 0), x, y);
            int r0 = 0, r1 = -1;
            g.rowRange(x - 0.1, y - 0.1, x + 0.1, y + 0.1, r0, r1);
            EXPECT_LT(r1 - r0 + 1, g.rows) << "tiling=" << static_cast<int>(g.type);
        }
    }
}

TEST(Tiling, NeighborSymmetryAllTilings) {
    const TilingGeom cases[] = {{TilingType::Square, 11, 7},
                                {TilingType::Hex, 10, 8},
                                {TilingType::Tri, 10, 8}};
    for (const auto& g : cases) {
        for (int idx = 0; idx < g.cellCount(); ++idx) {
            for (int k = 0; k < g.neighborCount(); ++k) {
                const int n = g.neighbor(idx, k);
                if (n < 0) continue;  // 界外
                bool back = false;
                for (int k2 = 0; k2 < g.neighborCount(); ++k2)
                    if (g.neighbor(n, k2) == idx) back = true;
                EXPECT_TRUE(back) << "tiling=" << static_cast<int>(g.type) << " idx=" << idx
                                  << " k=" << k << " n=" << n;
            }
        }
    }
}

TEST(Tiling, NeighborCounts) {
    const TilingGeom sq{TilingType::Square, 4, 4};
    const TilingGeom hx{TilingType::Hex, 4, 4};
    const TilingGeom tr{TilingType::Tri, 4, 4};
    EXPECT_EQ(sq.neighborCount(), 4);
    EXPECT_EQ(hx.neighborCount(), 6);
    EXPECT_EQ(tr.neighborCount(), 3);
}

TEST(Tiling, PointNeighborsIncludeExpectedGeometry) {
    const TilingGeom sq{TilingType::Square, 5, 5};
    const int center = 2 * 5 + 2;
    EXPECT_EQ(sq.pointNeighborCount(center), 8);
    std::set<int> squarePoints;
    for (int k = 0; k < sq.pointNeighborCount(center); ++k)
        squarePoints.insert(sq.pointNeighbor(center, k));
    EXPECT_EQ(squarePoints.size(), 8u);
    int squareBoundaryNeighbors = 0;
    for (int k = 0; k < sq.pointNeighborCount(0); ++k)
        if (sq.pointNeighbor(0, k) >= 0) ++squareBoundaryNeighbors;
    EXPECT_EQ(squareBoundaryNeighbors, 3);

    const TilingGeom hx{TilingType::Hex, 5, 6};
    const int hexCenter = hx.cellIndexAt(2, 2, 0);
    EXPECT_EQ(hx.pointNeighborCount(hexCenter), 6);
    std::set<int> hexPoints;
    for (int k = 0; k < hx.pointNeighborCount(hexCenter); ++k)
        hexPoints.insert(hx.pointNeighbor(hexCenter, k));
    std::set<int> hexEdges;
    for (int k = 0; k < hx.neighborCount(hexCenter); ++k)
        hexEdges.insert(hx.neighbor(hexCenter, k));
    EXPECT_EQ(hexPoints, hexEdges);

    const TilingGeom tr{TilingType::Tri, 8, 8};
    const int triCenter = tr.cellIndexAt(4, 3, 0);
    EXPECT_EQ(tr.pointNeighborCount(triCenter), 12);
    std::set<int> triPoints;
    for (int k = 0; k < tr.pointNeighborCount(triCenter); ++k) {
        const int nb = tr.pointNeighbor(triCenter, k);
        ASSERT_GE(nb, 0);
        triPoints.insert(nb);
    }
    EXPECT_EQ(triPoints.size(), 12u);
}

TEST(Tiling, RegularCellIncenterMatchesCenterAndUnitArea) {
    const TilingGeom cases[] = {{TilingType::Square, 5, 5},
                                {TilingType::Hex, 6, 6},
                                {TilingType::Tri, 8, 8}};
    for (const auto& g : cases) {
        for (int idx = 0; idx < g.cellCount(); ++idx) {
            double centerX, centerY, incenterX, incenterY;
            g.cellCenter(idx, centerX, centerY);
            g.cellIncenter(idx, incenterX, incenterY);
            EXPECT_DOUBLE_EQ(g.cellArea(idx), 1.0);
            EXPECT_DOUBLE_EQ(incenterX, centerX);
            EXPECT_DOUBLE_EQ(incenterY, centerY);
        }
    }
}

// 正方形几何回归：中心/取格/邻格序（0 下、1 上、2 左、3 右）与界外 -1。
TEST(Tiling, SquareGeometryRegression) {
    const TilingGeom g{TilingType::Square, 10, 6};
    double wx, wy;
    g.cellCenter(3 * 10 + 7, wx, wy);
    EXPECT_DOUBLE_EQ(wx, 7.5);
    EXPECT_DOUBLE_EQ(wy, 3.5);
    EXPECT_EQ(g.worldToCell(7.2, 3.8), 37);
    EXPECT_EQ(g.worldToCell(10.0 + 1e-4, 3.0), -1);
    EXPECT_EQ(g.worldToCell(-0.5, 3.0), -1);
    EXPECT_EQ(g.neighbor(37, 0), 47);  // 下 y+1
    EXPECT_EQ(g.neighbor(37, 1), 27);  // 上 y-1
    EXPECT_EQ(g.neighbor(37, 2), 36);  // 左 x-1
    EXPECT_EQ(g.neighbor(37, 3), 38);  // 右 x+1
    EXPECT_EQ(g.neighbor(0, 1), -1);   // 顶行上越界
    EXPECT_EQ(g.neighbor(59, 0), -1);  // 底行下越界
}

TEST(Tiling, HexOrthogonalBlockRepeatsTwoCells) {
    const TilingGeom g{TilingType::Hex, 6, 8};
    ASSERT_EQ(g.baseCount(), 2);
    double x0, y0, x1, y1, x2, y2;
    g.cellCenter(g.cellIndexAt(0, 0, 0), x0, y0);
    g.cellCenter(g.cellIndexAt(0, 0, 1), x1, y1);
    g.cellCenter(g.cellIndexAt(1, 0, 0), x2, y2);
    EXPECT_NEAR(x1 - x0, TilingGeom::kHexColSpacing / 2.0, 1e-9);
    EXPECT_NEAR(y1 - y0, TilingGeom::kHexRowSpacing, 1e-9);
    EXPECT_NEAR(x2 - x0, 0.0, 1e-9);
    EXPECT_NEAR(y2 - y0, 2.0 * TilingGeom::kHexRowSpacing, 1e-9);
}

TEST(Tiling, HexTopBottomGeometryAnyBlockRows) {
    for (const int rows : {7, 8}) {
        const TilingGeom g{TilingType::Hex, 6, rows};
        double vx[6], vy[6];
        ASSERT_EQ(g.cellPolygon(g.cellIndexAt(0, 0, 0), vx, vy, 6), 6);
        EXPECT_NEAR(*std::min_element(vy, vy + 6), TilingGeom::kHexSide / 2.0, 1e-9);
        ASSERT_EQ(g.cellPolygon(g.cellIndexAt(rows - 1, 0, 1), vx, vy, 6), 6);
        EXPECT_NEAR(*std::max_element(vy, vy + 6), g.worldHeight(), 1e-9);
    }
}

TEST(Tiling, HexBlockBoundaryNeighborsMatch) {
    const TilingGeom g{TilingType::Hex, 6, 8};
    const int upper = g.cellIndexAt(3, 2, 1);
    EXPECT_EQ(g.neighbor(upper, 1), g.cellIndexAt(4, 3, 0));
    EXPECT_EQ(g.neighbor(upper, 2), g.cellIndexAt(4, 2, 0));
}

// 三角形顶/底平：底行 y=0 为正三角底边；顶行 y=worldHeight 为反三角底边（上边）。
TEST(Tiling, TriTopBottomFlat) {
    const TilingGeom g{TilingType::Tri, 8, 6};
    // 底行 up：底边 y=0
    double wx, wy;
    g.cellCenter(g.cellIndexAt(0, 3, 0), wx, wy);
    EXPECT_NEAR(wy - g.kTriAlt / 3.0, 0.0, 1e-9);
    EXPECT_EQ(g.worldToCell(wx, wy), g.cellIndexAt(0, 3, 0));
    // 顶行 down：上边 y = worldHeight
    g.cellCenter(g.cellIndexAt(5, 4, 3), wx, wy);
    EXPECT_NEAR(wy + g.kTriAlt / 3.0, g.worldHeight(), 1e-9);
    EXPECT_EQ(g.worldToCell(wx, wy), g.cellIndexAt(5, 4, 3));
}

TEST(Tiling, TriOrthogonalBlockRepeatsFourCells) {
    const TilingGeom g{TilingType::Tri, 8, 6};
    EXPECT_EQ(g.baseCount(), 4);
    double x0, y0, x1, y1;
    g.cellCenter(g.cellIndexAt(2, 3, 0), x0, y0);
    g.cellCenter(g.cellIndexAt(2, 4, 0), x1, y1);
    EXPECT_NEAR(x1 - x0, g.kTriSide, 1e-9);
    EXPECT_NEAR(y1 - y0, 0.0, 1e-9);
    g.cellCenter(g.cellIndexAt(3, 3, 0), x1, y1);
    EXPECT_NEAR(x1 - x0, 0.0, 1e-9);
    EXPECT_NEAR(y1 - y0, 2.0 * g.kTriAlt, 1e-9);
}

// 穿越：六边形中心向右 → 边 0，t = √3a/2。
TEST(Tiling, CrossEdgeHexRight) {
    const TilingGeom g{TilingType::Hex, 10, 8};
    const int idx = g.cellIndexAt(3, 4, 0);
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, 0.0, rem);
    EXPECT_EQ(k, 0);
    EXPECT_NEAR(rem, 5.0 - TilingGeom::kHexColSpacing / 2.0, 1e-9);
    EXPECT_NEAR(x, wx + TilingGeom::kHexColSpacing / 2.0, 1e-9);
    // 穿过后下一格 = neighbor(0)，且位置微推进后取格一致
    EXPECT_EQ(g.neighbor(idx, 0), g.worldToCell(x + 1e-4, y));
}

// 穿越：六边形中心正上 → 顶点命中（3 格共点）：位置推进到顶点、返回 -1；
// 调用方从顶点 nudge ε 后 worldToCell 进入顶点另一侧格（确定性）。
TEST(Tiling, CrossEdgeHexUpVertexPass) {
    const TilingGeom g{TilingType::Hex, 10, 8};
    const int idx = g.cellIndexAt(3, 4, 0);
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, lw::kPi / 2.0, rem);
    EXPECT_EQ(k, -1);                                    // 顶点
    EXPECT_NEAR(y, wy + TilingGeom::kHexSide, 1e-9);     // 位置已到顶顶点
    EXPECT_NEAR(rem, 5.0 - TilingGeom::kHexSide, 1e-9);
    // 从顶点 nudge 后重定位：应进入顶点上方某格（非本格）
    const int nxt = g.worldToCell(x + 1e-4, y + 1e-4);
    EXPECT_GE(nxt, 0);
    EXPECT_NE(nxt, idx);
}

// 穿越：正三角中心向下 → 底边 k=0，t = h/3，进入 down(i, r-1)。
TEST(Tiling, CrossEdgeTriUpDown) {
    const TilingGeom g{TilingType::Tri, 10, 8};
    const int idx = g.cellIndexAt(3, 4, 0);
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, -lw::kPi / 2.0, rem);
    EXPECT_EQ(k, 0);
    EXPECT_NEAR(rem, 5.0 - g.kTriAlt / 3.0, 1e-9);
    EXPECT_EQ(g.neighbor(idx, 0), g.worldToCell(x + kEps, y - kEps));
    // 底行（r=0）的 up 下穿 → 越界 -1（图外）
    const int bIdx = g.cellIndexAt(0, 4, 0);
    g.cellCenter(bIdx, wx, wy);
    x = wx;
    y = wy;
    rem = 5.0;
    EXPECT_EQ(g.crossEdge(bIdx, x, y, -lw::kPi / 2.0, rem), 0);
    EXPECT_EQ(g.neighbor(bIdx, 0), -1);
}

// 穿越：正三角中心正上 → 顶点命中（6 格共点）：位置推进到顶点、返回 -1；
// 从顶点 nudge 后进入顶点上方格（r=3 奇 → up(5, 4)）。
TEST(Tiling, CrossEdgeTriUpUpVertexPass) {
    const TilingGeom g{TilingType::Tri, 10, 8};
    const int idx = g.cellIndexAt(1, 4, 2);
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, lw::kPi / 2.0, rem);
    EXPECT_EQ(k, -1);
    EXPECT_NEAR(y, wy + 2.0 * g.kTriAlt / 3.0, 1e-9);  // 位置已到顶点（上尖）
    EXPECT_NEAR(rem, 5.0 - 2.0 * g.kTriAlt / 3.0, 1e-9);
    const int nxt = g.worldToCell(x + 1e-4, y + 1e-4);
    EXPECT_EQ(nxt, g.cellIndexAt(2, 5, 0));
}

// 穿越：反三角中心向上 → 顶边 k=0，进入 up(i, r+1)（P12 直边布局：同列上邻）。
TEST(Tiling, CrossEdgeTriDownUp) {
    const TilingGeom g{TilingType::Tri, 10, 8};
    const int idx = g.cellIndexAt(1, 4, 3);
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, lw::kPi / 2.0, rem);
    EXPECT_EQ(k, 0);
    EXPECT_NEAR(rem, 5.0 - g.kTriAlt / 3.0, 1e-9);
    EXPECT_EQ(g.neighbor(idx, 0), g.cellIndexAt(2, 4, 0));
}

// 穿越：剩余长度不足 → -2 走完，位置推进 remLength。
TEST(Tiling, CrossEdgeWalkComplete) {
    const TilingGeom g{TilingType::Hex, 10, 8};
    const int idx = g.cellIndexAt(3, 4, 0);
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 0.1;
    const int k = g.crossEdge(idx, x, y, 0.0, rem);
    EXPECT_EQ(k, -2);
    EXPECT_NEAR(rem, 0.0, 1e-12);
    EXPECT_NEAR(x, wx + 0.1, 1e-9);
    EXPECT_NEAR(y, wy, 1e-9);
}

TEST(Tiling, TriNeighborFacts) {
    const TilingGeom g{TilingType::Tri, 10, 8};
    const int oddUp = g.cellIndexAt(3, 4, 2);
    EXPECT_EQ(g.neighbor(oddUp, 0), g.cellIndexAt(3, 4, 1));
    EXPECT_EQ(g.neighbor(oddUp, 1), g.cellIndexAt(3, 4, 3));
    EXPECT_EQ(g.neighbor(oddUp, 2), g.cellIndexAt(3, 5, 3));
    const int oddDown = g.cellIndexAt(3, 4, 3);
    EXPECT_EQ(g.neighbor(oddDown, 0), g.cellIndexAt(4, 4, 0));
    EXPECT_EQ(g.neighbor(oddDown, 1), g.cellIndexAt(3, 4, 2));
    EXPECT_EQ(g.neighbor(oddDown, 2), g.cellIndexAt(3, 3, 2));
    const int evenDown = g.cellIndexAt(3, 4, 1);
    EXPECT_EQ(g.neighbor(evenDown, 0), g.cellIndexAt(3, 4, 2));
    const int evenUp = g.cellIndexAt(3, 4, 0);
    EXPECT_EQ(g.neighbor(evenUp, 0), g.cellIndexAt(2, 4, 3));
    EXPECT_EQ(g.neighbor(evenUp, 1), g.cellIndexAt(3, 3, 1));
    EXPECT_EQ(g.neighbor(evenUp, 2), g.cellIndexAt(3, 4, 1));
}

// 边端点与邻格一致：cellEdge(idx,k) 的两端点必须是 neighbor(idx,k) 多边形的顶点（几何正确性）。
TEST(Tiling, CellEdgeMatchesNeighborVertices) {
    const TilingGeom cases[] = {{TilingType::Square, 10, 8},
                                {TilingType::Hex, 10, 8},
                                {TilingType::Tri, 10, 8}};
    for (const auto& g : cases) {
        for (int idx = 0; idx < g.cellCount(); ++idx) {
            for (int k = 0; k < g.neighborCount(); ++k) {
                const int nb = g.neighbor(idx, k);
                if (nb < 0) continue;
                double ex0, ey0, ex1, ey1;
                ASSERT_TRUE(g.cellEdge(idx, k, ex0, ey0, ex1, ey1));
                double vx[6], vy[6];
                const int n = g.cellPolygon(nb, vx, vy, 6);
                ASSERT_GE(n, 3);
                const auto isVertex = [&](double x, double y) {
                    for (int i = 0; i < n; ++i)
                        if (std::fabs(vx[i] - x) < 1e-9 && std::fabs(vy[i] - y) < 1e-9) return true;
                    return false;
                };
                EXPECT_TRUE(isVertex(ex0, ey0)) << "tiling=" << static_cast<int>(g.type)
                                                << " idx=" << idx << " k=" << k;
                EXPECT_TRUE(isVertex(ex1, ey1)) << "tiling=" << static_cast<int>(g.type)
                                                << " idx=" << idx << " k=" << k;
            }
        }
    }
}

// 剔除覆盖性：任意视口世界矩形，凡多边形与视口相交的格，其 (r,c) 必落在
// rowRange/colRange 结果内（P12：防"固定区域纯黑"——剔除漏格）。
TEST(Tiling, CullingCoversAllVisibleCells) {
    const TilingGeom cases[] = {{TilingType::Hex, 20, 20}, {TilingType::Tri, 20, 20}};
    for (const auto& g : cases) {
        const double ww = g.worldWidth(), wh = g.worldHeight();
        // 采样多种视口（整图、半图、1/4 图、细条），位置沿两轴滑动。
        for (const double vw : {ww, ww * 0.5, ww * 0.25, 4.0}) {
            for (const double vh : {wh, wh * 0.5, wh * 0.25, 4.0}) {
                for (double cy = 0.0; cy <= wh; cy += std::max(1.0, vh * 0.5)) {
                    for (double cx = 0.0; cx <= ww; cx += std::max(1.0, vw * 0.5)) {
                        const double vx0 = cx - vw * 0.5, vx1 = cx + vw * 0.5;
                        const double vy0 = cy - vh * 0.5, vy1 = cy + vh * 0.5;
                        if (vx1 <= 0.0 || vx0 >= ww || vy1 <= 0.0 || vy0 >= wh) continue;
                        int r0, r1;
                        g.rowRange(vx0, vy0, vx1, vy1, r0, r1);
                        if (r0 > r1) continue;
                        for (int idx = 0; idx < g.cellCount(); ++idx) {
                            double vx[6], vy[6];
                            const int n = g.cellPolygon(idx, vx, vy, 6);
                            double minX = vx[0], maxX = vx[0], minY = vy[0], maxY = vy[0];
                            for (int i = 1; i < n; ++i) {
                                minX = std::min(minX, vx[i]);
                                maxX = std::max(maxX, vx[i]);
                                minY = std::min(minY, vy[i]);
                                maxY = std::max(maxY, vy[i]);
                            }
                            if (maxX <= vx0 || minX >= vx1 || maxY <= vy0 || minY >= vy1) continue;
                            int rr = -1, cc = -1, bb = -1;
                            g.indexToRowCol(idx, rr, cc, bb);
                            int c0, c1;
                            g.colRange(vx0, vx1, rr, c0, c1);
                            EXPECT_TRUE(rr >= r0 && rr <= r1 && cc >= c0 && cc <= c1)
                                << "tiling=" << static_cast<int>(g.type) << " idx=" << idx
                                << " view=(" << vx0 << "," << vy0 << ")-(" << vx1 << "," << vy1
                                << ") r=" << rr << " c=" << cc << " range r[" << r0 << "," << r1
                                << "] c[" << c0 << "," << c1 << "]";
                        }
                    }
                }
            }
        }
    }
}

// 六边形轴向偏移 → 世界位置（形状表用）：轴向 (dq,dr) 世界偏移 =
// (√3a(dq + dr/2), 1.5a·dr)，锚格中心 + 偏移应解析回同一格。
TEST(Tiling, HexAxialOffsetResolves) {
    const TilingGeom g{TilingType::Hex, 10, 8};
    const int anchor = g.cellIndexAt(3, 4, 0);
    double ax, ay;
    g.cellCenter(anchor, ax, ay);
    // 轴向 (0,-1)（下方）与 (1,-1)（右下）：L3 形状 {(0,0),(0,-1),(1,-1)}
    const struct { int dq, dr; } offs[3] = {{0, 0}, {0, -1}, {1, -1}};
    for (const auto& o : offs) {
        const double dx = TilingGeom::kHexColSpacing * (static_cast<double>(o.dq) + 0.5 * o.dr);
        const double dy = TilingGeom::kHexRowSpacing * static_cast<double>(o.dr);
        const int got = g.worldToCell(ax + dx, ay + dy);
        EXPECT_GE(got, 0);
        // 邻接锚格（(0,0) 除外）
        if (!(o.dq == 0 && o.dr == 0)) {
            bool adjacent = false;
            for (int k = 0; k < 6; ++k)
                if (g.neighbor(anchor, k) == got) adjacent = true;
            EXPECT_TRUE(adjacent) << "offset dq=" << o.dq << " dr=" << o.dr;
        }
    }
}

// ---- 城市形状表（Config::City 按密铺；P12 §8.1 不变量）----

namespace {

// 形状 → 格下标（锚点取地图中部；不做运行时朝向变换）。
std::vector<int> shapeToCells(const lw::Config::City::Shape& shape, const TilingGeom& g,
                              int anchorBase = 0) {
    const int anchor = g.cellIndexAt(6, 10, anchorBase);
    double ax, ay;
    g.cellCenter(anchor, ax, ay);
    std::vector<int> out;
    for (const auto& c : shape.cells) {
        const double wx = ax + c.dx;
        out.push_back(g.worldToCell(wx, ay + c.dy));
    }
    return out;
}

// 连通性：从格 0 BFS，全部可达（邻接 = 边邻）。
bool shapeConnected(const TilingGeom& g, const std::vector<int>& cells) {
    std::vector<bool> vis(cells.size(), false);
    std::vector<int> stack{0};
    vis[0] = true;
    int seen = 0;
    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        ++seen;
        for (int k = 0; k < g.neighborCount(); ++k) {
            const int nb = g.neighbor(cells[static_cast<size_t>(cur)], k);
            if (nb < 0) continue;
            for (std::size_t j = 0; j < cells.size(); ++j)
                if (!vis[j] && cells[j] == nb) {
                    vis[j] = true;
                    stack.push_back(static_cast<int>(j));
                }
        }
    }
    return seen == static_cast<int>(cells.size());
}

}  // namespace

// 形状表不变量：shapeLevelIndex 关联等级；同级可有多个显式形状变体。
TEST(Tiling, CityShapeCountEqualsLevel) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const lw::Config::City::TilingSet* sets[3] = {&cfg.city.square, &cfg.city.hex, &cfg.city.tri};
    const int expectLv[3][6] = {{1, 2, 4, 6, 9}, {1, 3, 4, 6, 7, 9}, {1, 2, 4, 6, 8}};
    for (int t = 0; t < 3; ++t) {
        const auto& set = *sets[t];
        ASSERT_EQ(set.shapeLevelIndex.size(), set.shapes.size());
        for (std::size_t i = 0; i < set.levels.size(); ++i) {
            EXPECT_EQ(set.levels[i], expectLv[t][i]) << "tiling " << t;
        }
        for (std::size_t i = 0; i < set.shapes.size(); ++i) {
            const int levelIndex = set.shapeLevelIndex[i];
            ASSERT_GE(levelIndex, 0);
            ASSERT_LT(levelIndex, static_cast<int>(set.levels.size()));
            const double level = set.levels[static_cast<std::size_t>(levelIndex)];
            EXPECT_EQ(set.shapes[i].cells.size(), static_cast<std::size_t>(std::lround(level)))
                << "tiling " << t << " shape " << i << " level " << level;
            const auto anchor = std::find_if(set.shapes[i].cells.begin(), set.shapes[i].cells.end(),
                                             [](const auto& cell) {
                return cell.dx == 0.0 && cell.dy == 0.0;
            });
            EXPECT_NE(anchor, set.shapes[i].cells.end());
        }
    }
}

// 六/三角形状连通性（在对应密铺地图上解析后 BFS）；三角测正/反两种锚朝向（变体模式）。
TEST(Tiling, CityHexShapesConnected) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const TilingGeom g{TilingType::Hex, 24, 14};
    for (std::size_t shapeIndex = 0; shapeIndex < cfg.city.hex.shapes.size(); ++shapeIndex) {
        const std::vector<int> cells = shapeToCells(cfg.city.hex.shapes[shapeIndex], g);
        EXPECT_TRUE(shapeConnected(g, cells)) << "hex shape " << shapeIndex;
    }
}

TEST(Tiling, CityTriShapesConnectedForExplicitAnchorBases) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const TilingGeom g{TilingType::Tri, 24, 14};
    for (std::size_t shapeIndex = 0; shapeIndex < cfg.city.tri.shapes.size(); ++shapeIndex) {
        const auto& shape = cfg.city.tri.shapes[shapeIndex];
        for (int anchorBase = 0; anchorBase < g.baseCount(); ++anchorBase) {
            if (shape.anchorBaseMask != 0 &&
                (shape.anchorBaseMask & (1u << anchorBase)) == 0)
                continue;
            const std::vector<int> cells = shapeToCells(shape, g, anchorBase);
            EXPECT_TRUE(shapeConnected(g, cells))
                << "shape " << shapeIndex << " base " << anchorBase;
        }
    }
}

// 三角形 L2：显式允许的两个正三角基础格都解析为水平公共边。
TEST(Tiling, TriL2HorizontalSharedEdge) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const TilingGeom g{TilingType::Tri, 24, 14};
    const auto* shape = cfg.city.tri.shapeFor(2, 0);
    ASSERT_NE(shape, nullptr);
    for (int anchorBase : {0, 2}) {
        const std::vector<int> cells = shapeToCells(*shape, g, anchorBase);
        ASSERT_EQ(cells.size(), 2u) << "base " << anchorBase;
        EXPECT_EQ(cells[1], g.neighbor(cells[0], 0)) << "base " << anchorBase;
        double ex0, ey0, ex1, ey1;
        ASSERT_TRUE(g.cellEdge(cells[0], 0, ex0, ey0, ex1, ey1));
        EXPECT_NEAR(ey0, ey1, 1e-9) << "base " << anchorBase;
    }
}

TEST(Tiling, TriL4HasExplicitUpAndDownVariants) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    ASSERT_EQ(cfg.city.tri.variantCount(4), 2);
    const auto* down = cfg.city.tri.shapeFor(4, 0);
    const auto* up = cfg.city.tri.shapeFor(4, 1);
    ASSERT_NE(down, nullptr);
    ASSERT_NE(up, nullptr);
    EXPECT_EQ(down->anchorBaseMask, (1u << 1) | (1u << 3));
    EXPECT_EQ(up->anchorBaseMask, (1u << 0) | (1u << 2));
}

}  // namespace
