// test_tiling.cpp — 密铺纯几何单测（P12；开发计划 §8.1 测试先行）。
// 只链 landwar_core，无 SDL/无 RNG：中心往返、邻接对称、环绕一致性、
// 三角形顶/底平、穿越求交（含顶点平局确定性）、城市形状表（格数=等级、连通）。
#include <gtest/gtest.h>

#include <cmath>

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

// 正方形几何回归：中心/取格/邻格序（0 下、1 上、2 左、3 右）与界外 -1。
TEST(Tiling, SquareGeometryRegression) {
    const TilingGeom g{TilingType::Square, 10, 6};
    double wx, wy;
    g.cellCenter(3 * 10 + 7, wx, wy);
    EXPECT_DOUBLE_EQ(wx, 7.5);
    EXPECT_DOUBLE_EQ(wy, 3.5);
    EXPECT_EQ(g.worldToCell(7.2, 3.8), 37);
    EXPECT_EQ(g.worldToCell(10.0, 3.0), -1);
    EXPECT_EQ(g.worldToCell(-0.5, 3.0), -1);
    EXPECT_EQ(g.neighbor(37, 0), 47);  // 下 y+1
    EXPECT_EQ(g.neighbor(37, 1), 27);  // 上 y-1
    EXPECT_EQ(g.neighbor(37, 2), 36);  // 左 x-1
    EXPECT_EQ(g.neighbor(37, 3), 38);  // 右 x+1
    EXPECT_EQ(g.neighbor(0, 1), -1);   // 顶行上越界
    EXPECT_EQ(g.neighbor(59, 0), -1);  // 底行下越界
}

// 六边形偶数行：顶行顶点贴 worldHeight；环绕后落入行 0。
TEST(Tiling, HexWrapVerticalEvenRows) {
    const TilingGeom g{TilingType::Hex, 6, 8};  // H=8 偶数
    double wx, wy;
    g.cellCenter(7 * 6 + 0, wx, wy);  // 顶行 r=7
    const double topY = wy + TilingGeom::kHexSide;
    EXPECT_NEAR(topY, g.worldHeight(), 1e-9);
    // 环绕 y -= worldHeight → 应落在底行（y ∈ [-a/2, ...]）某格
    const double wy2 = topY - g.worldHeight();
    EXPECT_GE(wy2, -TilingGeom::kHexSide - 1e-9);
    const int idx = g.worldToCell(wx, wy2);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, g.cellCount());
    EXPECT_EQ(idx / 6, 0);  // 行 0
}

// 奇数行（H=7）时顶/底均无顶点对称性差异（顶恒贴 worldHeight、底恒凸出 a/2）；
// 偶数行要求来自**环绕接缝邻接一致性**（见 HexVerticalWrapSeamEvenRows）。
TEST(Tiling, HexTopBottomGeometryAnyRows) {
    for (const int rows : {7, 8}) {
        const TilingGeom g{TilingType::Hex, 6, rows};
        double wx, wy;
        g.cellCenter((rows - 1) * 6 + 0, wx, wy);
        EXPECT_NEAR(wy + TilingGeom::kHexSide, g.worldHeight(), 1e-9);  // 顶行顶点贴 worldHeight
        g.cellCenter(0 * 6 + 0, wx, wy);
        EXPECT_NEAR(wy - TilingGeom::kHexSide, -TilingGeom::kHexSide / 2.0, 1e-9);  // 底行底顶点 -a/2
    }
}

// 六边形偶数行环绕接缝：顶行（奇）格中心正上穿越 → 顶点（k 不确定）→ 环绕 y 后
// 应落入行 0 的公式一致格（(0, c+(r&1))）。
TEST(Tiling, HexVerticalWrapSeamEvenRows) {
    const TilingGeom g{TilingType::Hex, 6, 8};  // H 偶
    const int idx = 7 * 6 + 2;                  // 顶行 r=7（奇）c=2
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    // 越过顶行顶顶点：y = worldHeight + ε（环绕后 ≈ 0）
    const double y2 = wy + TilingGeom::kHexSide + 1e-6;
    const int got = g.worldToCell(wx, y2 - g.worldHeight());
    // 上邻公式：(r+1=8≡0, c' = c+(7&1) = 3) → (0, 3)；位置环绕结果与之一致
    EXPECT_EQ(got, 0 * 6 + 3);
}

// 三角形顶/底平：底行 y=0 为正三角底边；顶行 y=worldHeight 为反三角底边（上边）。
TEST(Tiling, TriTopBottomFlat) {
    const TilingGeom g{TilingType::Tri, 8, 6};
    // 底行 up：底边 y=0
    double wx, wy;
    g.cellCenter(2 * (0 * 8 + 3) + 0, wx, wy);
    EXPECT_NEAR(wy - g.kTriAlt / 3.0, 0.0, 1e-9);
    EXPECT_EQ(g.worldToCell(wx, wy), 2 * (0 * 8 + 3) + 0);
    // 顶行 down：上边 y = worldHeight
    g.cellCenter(2 * (5 * 8 + 4) + 1, wx, wy);
    EXPECT_NEAR(wy + g.kTriAlt / 3.0, g.worldHeight(), 1e-9);
    EXPECT_EQ(g.worldToCell(wx, wy), 2 * (5 * 8 + 4) + 1);
}

// 三角形左右互补：底行最右 down 环绕 -W 后与最左 up 共享斜边
// （down 顶点 x − W = 0 = up 底左角 x；down 底右角 x − W = b/2 = up 顶点 x）。
TEST(Tiling, TriWrapLeftRightComplementary) {
    const TilingGeom g{TilingType::Tri, 8, 6};
    double wxUp, wyUp;
    g.cellCenter(2 * (0 * 8 + 0) + 0, wxUp, wyUp);   // 最左 up：底左角 (0,0)、顶点 (b/2, h)
    double wxDn, wyDn;
    g.cellCenter(2 * (0 * 8 + 7) + 1, wxDn, wyDn);   // 最右 down：顶点 (8b, 0)、底右角 (8b+b/2, h)
    // down 顶点（x = 8b）环绕 −W = −8b → 0 = up 底左角
    EXPECT_NEAR(wxDn - g.worldWidth(), 0.0, 1e-9);
    // down 底右角（x = 8b + b/2）环绕 −W → b/2 = up 顶点 x
    EXPECT_NEAR(wxDn + g.kTriSide / 2.0 - g.worldWidth(), wxUp, 1e-9);
}

// 穿越：六边形中心向右 → 边 0，t = √3a/2。
TEST(Tiling, CrossEdgeHexRight) {
    const TilingGeom g{TilingType::Hex, 10, 8};
    const int idx = 3 * 10 + 4;
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, 0.0, rem);
    EXPECT_EQ(k, 0);
    EXPECT_NEAR(rem, 5.0 - TilingGeom::kHexColSpacing / 2.0, 1e-9);
    EXPECT_NEAR(x, wx + TilingGeom::kHexColSpacing / 2.0, 1e-9);
    // 穿过后下一格 = neighbor(0)，且位置微推进后取格一致
    EXPECT_EQ(g.neighbor(idx, 0), g.worldToCell(x + kEps, y));
}

// 穿越：六边形中心正上 → 顶点命中（3 格共点）：位置推进到顶点、返回 -1；
// 调用方从顶点 nudge ε 后 worldToCell 进入顶点另一侧格（确定性）。
TEST(Tiling, CrossEdgeHexUpVertexPass) {
    const TilingGeom g{TilingType::Hex, 10, 8};
    const int idx = 3 * 10 + 4;
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, lw::kPi / 2.0, rem);
    EXPECT_EQ(k, -1);                                    // 顶点
    EXPECT_NEAR(y, wy + TilingGeom::kHexSide, 1e-9);     // 位置已到顶顶点
    EXPECT_NEAR(rem, 5.0 - TilingGeom::kHexSide, 1e-9);
    // 从顶点 nudge 后重定位：应进入顶点上方某格（非本格）
    const int nxt = g.worldToCell(x + kEps, y + kEps);
    EXPECT_GE(nxt, 0);
    EXPECT_NE(nxt, idx);
}

// 穿越：正三角中心向下 → 底边 k=0，t = h/3，进入 down(i, r-1)。
TEST(Tiling, CrossEdgeTriUpDown) {
    const TilingGeom g{TilingType::Tri, 10, 8};
    const int idx = 2 * (3 * 10 + 4) + 0;  // up(4, 3)
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, -lw::kPi / 2.0, rem);
    EXPECT_EQ(k, 0);
    EXPECT_NEAR(rem, 5.0 - g.kTriAlt / 3.0, 1e-9);
    EXPECT_EQ(g.neighbor(idx, 0), g.worldToCell(x + kEps, y - kEps));
    // 底行（r=0）的 up 下穿 → 越界 -1（图外）
    const int bIdx = 2 * (0 * 10 + 4) + 0;
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
    const int idx = 2 * (3 * 10 + 4) + 0;
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, lw::kPi / 2.0, rem);
    EXPECT_EQ(k, -1);
    EXPECT_NEAR(y, wy + 2.0 * g.kTriAlt / 3.0, 1e-9);  // 位置已到顶点（上尖）
    EXPECT_NEAR(rem, 5.0 - 2.0 * g.kTriAlt / 3.0, 1e-9);
    const int nxt = g.worldToCell(x + kEps, y + kEps);
    EXPECT_EQ(nxt, 2 * (4 * 10 + 5) + 0);
}

// 穿越：反三角中心向上 → 顶边 k=0，进入 up(i, r+1)（P12 直边布局：同列上邻）。
TEST(Tiling, CrossEdgeTriDownUp) {
    const TilingGeom g{TilingType::Tri, 10, 8};
    const int idx = 2 * (3 * 10 + 4) + 1;  // down(4, 3)，r=3 奇 → 上邻 up(4, 4)（i 不变）
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 5.0;
    const int k = g.crossEdge(idx, x, y, lw::kPi / 2.0, rem);
    EXPECT_EQ(k, 0);
    EXPECT_NEAR(rem, 5.0 - g.kTriAlt / 3.0, 1e-9);
    EXPECT_EQ(g.neighbor(idx, 0), 2 * (4 * 10 + 4) + 0);  // up(4, 4)
}

// 穿越：剩余长度不足 → -2 走完，位置推进 remLength。
TEST(Tiling, CrossEdgeWalkComplete) {
    const TilingGeom g{TilingType::Hex, 10, 8};
    const int idx = 3 * 10 + 4;
    double wx, wy;
    g.cellCenter(idx, wx, wy);
    double x = wx, y = wy, rem = 0.1;
    const int k = g.crossEdge(idx, x, y, 0.0, rem);
    EXPECT_EQ(k, -2);
    EXPECT_NEAR(rem, 0.0, 1e-12);
    EXPECT_NEAR(x, wx + 0.1, 1e-9);
    EXPECT_NEAR(y, wy, 1e-9);
}

// 三角特定邻接（P12 直边布局几何事实；p = r&1）：
//   up(i,r)：k0 底 → down(i, r-1)（同列下邻）；k1 左斜 → down(i+p-1, r)；k2 右斜 → down(i+p, r)。
//   down(i,r)：k0 顶 → up(i, r+1)（同列上邻）；k1 右斜 → up(i+1-p, r)；k2 左斜 → up(i-p, r)。
TEST(Tiling, TriNeighborFacts) {
    const TilingGeom g{TilingType::Tri, 10, 8};
    const int up = 2 * (3 * 10 + 4) + 0;    // up(4,3)，r=3 奇（p=1）
    EXPECT_EQ(g.neighbor(up, 0), 2 * (2 * 10 + 4) + 1);  // down(4,2)（同列下邻）
    EXPECT_EQ(g.neighbor(up, 1), 2 * (3 * 10 + 4) + 1);  // down(4,3)（i+p-1 = 4）
    EXPECT_EQ(g.neighbor(up, 2), 2 * (3 * 10 + 5) + 1);  // down(5,3)（i+p = 5）
    const int dn = 2 * (3 * 10 + 4) + 1;    // down(4,3)，r=3 奇（p=1）
    EXPECT_EQ(g.neighbor(dn, 0), 2 * (4 * 10 + 4) + 0);  // up(4,4)（同列上邻，与 r 奇偶无关）
    EXPECT_EQ(g.neighbor(dn, 1), 2 * (3 * 10 + 4) + 0);  // up(4,3)（i+1-p = 4）
    EXPECT_EQ(g.neighbor(dn, 2), 2 * (3 * 10 + 3) + 0);  // up(3,3)（i-p = 3）
    // 偶数行 r=2（p=0）：down(4,2) 顶邻 = up(4,3)（i 不变）
    const int dn2 = 2 * (2 * 10 + 4) + 1;
    EXPECT_EQ(g.neighbor(dn2, 0), 2 * (3 * 10 + 4) + 0);
    // 偶数行 r=2：up(4,2) 底邻 = down(4,1)（同列下邻）；左斜 = down(3,2)；右斜 = down(4,2)
    const int up2 = 2 * (2 * 10 + 4) + 0;
    EXPECT_EQ(g.neighbor(up2, 0), 2 * (1 * 10 + 4) + 1);  // down(4,1)
    EXPECT_EQ(g.neighbor(up2, 1), 2 * (2 * 10 + 3) + 1);  // down(3,2)
    EXPECT_EQ(g.neighbor(up2, 2), 2 * (2 * 10 + 4) + 1);  // down(4,2)
}

// 边端点与邻格一致：cellEdge(idx,k) 的两端点必须是 neighbor(idx,k) 多边形的顶点（几何正确性）。
TEST(Tiling, CellEdgeMatchesNeighborVertices) {
    const TilingGeom cases[] = {{TilingType::Hex, 10, 8}, {TilingType::Tri, 10, 8}};
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
    const TilingGeom cases[] = {{TilingType::Hex, 40, 40}, {TilingType::Tri, 40, 40}};
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
                        g.rowRange(vy0, vy1, r0, r1);
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
                            // 可见格：行/列须在范围内（六：idx = r*cols+c；三：pair）。
                            int rr = -1, cc = -1;
                            if (g.type == TilingType::Hex) {
                                rr = idx / g.cols;
                                cc = idx % g.cols;
                            } else {
                                rr = (idx >> 1) / g.cols;
                                cc = (idx >> 1) % g.cols;
                            }
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
    const int anchor = 3 * 10 + 4;  // (r=3, c=4)
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

// 形状 → 格下标（锚点取地图中部；P1.2 起 square/hex/tri 的 cells 统一为世界偏移，
// 三角反锚镜像 dy，与 Map::shapeCells 一致）。
std::vector<int> shapeToCells(const lw::Config::City::TilingSet& set, int level,
                              const TilingGeom& g, int anchorOrient = 0) {
    const auto* sh = [&]() -> const lw::Config::City::Shape* {
        const int i = set.levelIndex(level);
        return i < 0 ? nullptr : &set.shapes[static_cast<size_t>(i)];
    }();
    if (!sh) return {};
    const int anchor = (g.type == TilingType::Tri)
                           ? 2 * (6 * g.cols + 10) + anchorOrient  // 中部锚（正/反）
                           : 5 * g.cols + 10;
    double ax, ay;
    g.cellCenter(anchor, ax, ay);
    const bool anchorUp = anchorOrient == 0;
    std::vector<int> out;
    for (const auto& c : sh->cells) {
        const double wx = ax + c.dx;
        const double wy = ay + (anchorUp ? c.dy : -c.dy);
        out.push_back(g.worldToCell(wx, wy));
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

// 形状表不变量：各密铺 levels[i] ↔ shapes[i] 一一对应、格数恰 = 等级数。
TEST(Tiling, CityShapeCountEqualsLevel) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const lw::Config::City::TilingSet* sets[3] = {&cfg.city.square, &cfg.city.hex, &cfg.city.tri};
    const int expectLv[3][6] = {{1, 2, 4, 6, 9}, {1, 3, 4, 6, 7, 9}, {1, 2, 4, 6, 8}};
    const int counts[3][6] = {{1, 2, 4, 6, 9, -1}, {1, 3, 4, 6, 7, 9}, {1, 2, 4, 6, 8, -1}};
    for (int t = 0; t < 3; ++t) {
        const auto& set = *sets[t];
        ASSERT_EQ(set.levels.size(), set.shapes.size());
        for (std::size_t i = 0; i < set.levels.size(); ++i) {
            EXPECT_EQ(set.levels[i], expectLv[t][i]) << "tiling " << t;
            EXPECT_EQ(set.shapes[i].cells.size(),
                      static_cast<std::size_t>(counts[t][i]))
                << "tiling " << t << " level " << set.levels[i];
            // cells[0] = 锚格 (0,0)。
            EXPECT_EQ(set.shapes[i].cells[0].dx, 0.0);
            EXPECT_EQ(set.shapes[i].cells[0].dy, 0.0);
        }
    }
}

// 六/三角形状连通性（在对应密铺地图上解析后 BFS）；三角测正/反两种锚朝向（变体模式）。
TEST(Tiling, CityHexShapesConnected) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const TilingGeom g{TilingType::Hex, 24, 14};
    for (int lv : cfg.city.hex.levels) {
        const std::vector<int> cells = shapeToCells(cfg.city.hex, lv, g);
        ASSERT_EQ(cells.size(), static_cast<std::size_t>(lv)) << "level " << lv;
        EXPECT_TRUE(shapeConnected(g, cells)) << "hex level " << lv;
    }
}

TEST(Tiling, CityTriShapesConnectedBothModes) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const TilingGeom g{TilingType::Tri, 24, 14};
    for (int orient = 0; orient <= 1; ++orient) {
        for (int lv : cfg.city.tri.levels) {
            const std::vector<int> cells = shapeToCells(cfg.city.tri, lv, g, orient);
            ASSERT_EQ(cells.size(), static_cast<std::size_t>(lv))
                << "orient " << orient << " level " << lv;
            EXPECT_TRUE(shapeConnected(g, cells)) << "orient " << orient << " tri level " << lv;
        }
    }
}

// 三角形 L2：一正一反、公共边**水平**（正格 k0 底 = 同列下方反格，两格共水平底边）。
// 反锚（镜像）变体：反格 + 同列上方正格（反格 k0 顶邻），公共边仍水平。
TEST(Tiling, TriL2HorizontalSharedEdge) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const TilingGeom g{TilingType::Tri, 24, 14};
    for (int orient = 0; orient <= 1; ++orient) {
        const std::vector<int> cells = shapeToCells(cfg.city.tri, 2, g, orient);
        ASSERT_EQ(cells.size(), 2u) << "orient " << orient;
        // 两格必为 k0 邻（正 k0 底 / 反 k0 顶：同列上下、共水平底边）
        EXPECT_EQ(cells[1], g.neighbor(cells[0], 0)) << "orient " << orient;
        double ex0, ey0, ex1, ey1;
        ASSERT_TRUE(g.cellEdge(cells[0], 0, ex0, ey0, ex1, ey1));
        EXPECT_NEAR(ey0, ey1, 1e-9) << "公共边应为水平（两端点 y 相等），orient " << orient;
    }
}

// 三角形 L4 正/反两种模式：正锚 = 大三角尖朝上（4 格 = 2 正 + 反 + 正）；反锚镜像 = 尖朝下。
// 各模式格数 = 4 且连通（连通性已由 CityTriShapesConnectedBothModes 覆盖），此处锁定模式几何：
// 正模式最上格为"正"，反模式最下格为"反"（朝向随镜像翻转）。
TEST(Tiling, TriL4ModesOrientation) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const TilingGeom g{TilingType::Tri, 24, 14};
    const std::vector<int> up = shapeToCells(cfg.city.tri, 4, g, 0);
    const std::vector<int> dn = shapeToCells(cfg.city.tri, 4, g, 1);
    ASSERT_EQ(up.size(), 4u);
    ASSERT_EQ(dn.size(), 4u);
    // 正模式：大三角最高点格（尖朝上 = 偶下标正格）存在且比锚格高 1 行；
    // 反模式：最低点格（尖朝下 = 奇下标反格）比锚格低 1 行。
    bool upHasTop = false, dnHasBottom = false;
    const auto rowOf = [&](int idx) { return (idx >> 1) / g.cols; };
    for (int idx : up)
        if ((idx & 1) == 0 && rowOf(idx) == 6 + 1) upHasTop = true;   // 行 7 正格（尖朝上）
    for (int idx : dn)
        if ((idx & 1) == 1 && rowOf(idx) == 6 - 1) dnHasBottom = true;  // 行 5 反格（尖朝下）
    EXPECT_TRUE(upHasTop);
    EXPECT_TRUE(dnHasBottom);
}

}  // namespace
