// test_tiling_table.cpp — 表驱动半正/Laves 密铺几何单测（读取 data/tiling_specs_arch.json）。
// 覆盖：格数、世界尺寸、中心→worldToCell 往返、邻接对称/边界、cellPolygon/cellEdge 一致性。
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "core/GameDefs.h"
#include "world/tiling/Tiling.h"

namespace {

using namespace lw;

const TilingType kTableTypes[] = {TilingType::Arch3464, TilingType::Arch3636,
                                  TilingType::Arch31212, TilingType::Arch4612,
                                  TilingType::Arch488};

TEST(TilingTable, ArchSpecsLoadAndCellRoundTrip) {
    for (TilingType t : kTableTypes) {
        TilingGeom g{t, 4, 3};
        ASSERT_GT(g.cellCount(), 0) << tilingName(t);
        EXPECT_GT(g.worldWidth(), 0.0) << tilingName(t);
        EXPECT_GT(g.worldHeight(), 0.0) << tilingName(t);
        for (int idx = 0; idx < g.cellCount(); ++idx) {
            double cx, cy;
            g.cellCenter(idx, cx, cy);
            const int back = g.worldToCell(cx, cy);
            EXPECT_EQ(back, idx) << tilingName(t) << " idx=" << idx << " c=(" << cx << "," << cy << ")";
        }
    }
}

TEST(TilingTable, ArchNeighborSymmetryAndBoundary) {
    for (TilingType t : kTableTypes) {
        TilingGeom g{t, 4, 3};
        for (int idx = 0; idx < g.cellCount(); ++idx) {
            const int n = g.neighborCount(idx);
            for (int k = 0; k < n; ++k) {
                const int nb = g.neighbor(idx, k);
                if (nb < 0) {
                    int r, c;
                    g.neighborRaw(idx, k, r, c);
                    EXPECT_TRUE(r < 0 || r >= g.rows || c < 0 || c >= g.cols)
                        << tilingName(t) << " idx=" << idx << " k=" << k;
                    continue;
                }
                bool found = false;
                for (int kk = 0; kk < g.neighborCount(nb); ++kk)
                    if (g.neighbor(nb, kk) == idx) found = true;
                EXPECT_TRUE(found) << tilingName(t) << " idx=" << idx << " k=" << k << " nb=" << nb;
            }
        }
    }
}

TEST(TilingTable, ArchCellPolygonMatchesCellEdge) {
    for (TilingType t : kTableTypes) {
        TilingGeom g{t, 3, 2};
        for (int idx = 0; idx < g.cellCount(); ++idx) {
            double vx[12], vy[12];
            const int n = g.cellPolygon(idx, vx, vy, 12);
            EXPECT_EQ(n, g.neighborCount(idx)) << tilingName(t) << " idx=" << idx;
            for (int k = 0; k < n; ++k) {
                double x0, y0, x1, y1;
                ASSERT_TRUE(g.cellEdge(idx, k, x0, y0, x1, y1))
                    << tilingName(t) << " idx=" << idx << " k=" << k;
                // 表驱动边 k 与顶点 k→k+1 同序。
                EXPECT_NEAR(x0, vx[k], 1e-9);
                EXPECT_NEAR(y0, vy[k], 1e-9);
                EXPECT_NEAR(x1, vx[(k + 1) % n], 1e-9);
                EXPECT_NEAR(y1, vy[(k + 1) % n], 1e-9);
            }
        }
    }
}

TEST(TilingTable, ArchRowColRangeConservative) {
    for (TilingType t : kTableTypes) {
        TilingGeom g{t, 5, 4};
        const double w = g.worldWidth(), h = g.worldHeight();
        for (int y0 = 0; y0 < 4; ++y0) {
            const double yy = h * (0.2 + 0.15 * y0);
            int r0, r1;
            g.rowRange(yy, yy, r0, r1);
            EXPECT_LE(r0, r1) << tilingName(t);
            EXPECT_GE(r0, 0);
            EXPECT_LE(r1, g.rows - 1);
            for (int r = r0; r <= r1; ++r) {
                int c0, c1;
                g.colRange(0.0, w, r, c0, c1);
                EXPECT_GE(c0, 0);
                EXPECT_LE(c1, g.cols - 1);
                EXPECT_LE(c0, c1);
            }
        }
    }
}

}  // namespace
