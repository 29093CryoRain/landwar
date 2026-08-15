// test_tiling_table.cpp — 表驱动半正/Laves 密铺几何单测（读取 data/tiling_specs_arch.json）。
// 覆盖：格数、世界尺寸、中心→worldToCell 往返、邻接对称/边界、cellPolygon/cellEdge 一致性。
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "core/Config.h"
#include "core/GameDefs.h"
#include "world/Map.h"
#include "world/MapGenerator.h"
#include "world/tiling/Tiling.h"

namespace {

using namespace lw;

const TilingType kTableTypes[] = {
    TilingType::Arch3464,  TilingType::Arch3636,  TilingType::Arch31212,
    TilingType::Arch4612,  TilingType::Arch488,   TilingType::Laves3464,
    TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
    TilingType::Laves488};

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

TEST(TilingTable, Arch488MapGenerationAndCapitalPlacement) {
    const Config cfg = Config::loadFromJson("{}");
    MapGenParams gp{32, 32, 0.4, 0.1, 0.02, false, TilingType::Arch488};
    const std::string path = "userdata/maps/utest_arch488.lwmap";
    ASSERT_TRUE(MapGenerator::generate(path, 42, gp));
    Config::Map mc = cfg.map;
    mc.tiling = "arch_488";
    mc.width = 32;
    mc.height = 32;
    mc.file = path;
    Map map;
    map.configure(mc);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    Rng rng(42);
    ASSERT_TRUE(map.loadFromLwmap(path, rng));
    ASSERT_TRUE(map.placeCapitals(rng));
    map.finalize();
    EXPECT_EQ(map.cellCount(), map.geom().baseCount() * 32 * 32);
    EXPECT_GE(map.cityCount(), 8);  // 至少 8 个首都城
}

TEST(TilingTable, Laves488MapGenerationAndCapitalPlacement) {
    const Config cfg = Config::loadFromJson("{}");
    MapGenParams gp{32, 32, 0.4, 0.1, 0.02, false, TilingType::Laves488};
    const std::string path = "userdata/maps/utest_laves488.lwmap";
    ASSERT_TRUE(MapGenerator::generate(path, 42, gp));
    Config::Map mc = cfg.map;
    mc.tiling = "laves_488";
    mc.width = 32;
    mc.height = 32;
    mc.file = path;
    Map map;
    map.configure(mc);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    Rng rng(42);
    ASSERT_TRUE(map.loadFromLwmap(path, rng));
    ASSERT_TRUE(map.placeCapitals(rng));
    map.finalize();
    EXPECT_EQ(map.cellCount(), map.geom().baseCount() * 32 * 32);
    EXPECT_GE(map.cityCount(), 8);
}

TEST(TilingTable, Arch488CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "arch_488";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch488))];
    ASSERT_EQ(set.levels.size(), 5u);
    const int expectedCells[5] = {1, 5, 4, 5, 9};  // L2/L3/L4/L7/L8
    for (int i = 0; i < 5; ++i) {
        const int anchorN = set.shapes[static_cast<size_t>(i)].anchorN;
        // 找地图中部一个 anchorN 边格。
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "level index " << i;
        const auto cells = map.shapeCells(set.levels[static_cast<size_t>(i)], anchor);
        // 过滤界外 -1。
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, expectedCells[i]) << "level index " << i;
    }
}

TEST(TilingTable, Laves3636CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "laves_3636";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Laves3636))];
    ASSERT_EQ(set.levels.size(), 5u);
    const int expectedCells[5] = {1, 3, 5, 6, 12};
    for (int i = 0; i < 5; ++i) {
        const int anchorN = set.shapes[static_cast<size_t>(i)].anchorN;
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "level index " << i;
        const auto cells = map.shapeCells(set.levels[static_cast<size_t>(i)], anchor);
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, expectedCells[i]) << "level index " << i;
    }
}

TEST(TilingTable, Laves488CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "laves_488";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Laves488))];
    ASSERT_EQ(set.levels.size(), 3u);
    const int expectedCells[3] = {2, 4, 8};
    for (int i = 0; i < 3; ++i) {
        const int anchorN = set.shapes[static_cast<size_t>(i)].anchorN;
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "level index " << i;
        const auto cells = map.shapeCells(set.levels[static_cast<size_t>(i)], anchor);
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, expectedCells[i]) << "level index " << i;
    }
}

TEST(TilingTable, Arch3636CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "arch_3636";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch3636))];
    ASSERT_EQ(set.levels.size(), 4u);
    const int expectedCells[4] = {1, 3, 4, 7};
    for (int i = 0; i < 4; ++i) {
        const int anchorN = set.shapes[static_cast<size_t>(i)].anchorN;
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "level index " << i;
        const auto cells = map.shapeCells(set.levels[static_cast<size_t>(i)], anchor);
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, expectedCells[i]) << "level index " << i;
    }
}

TEST(TilingTable, Laves3464CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "laves_3464";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Laves3464))];
    ASSERT_EQ(set.levels.size(), 4u);
    const int expectedCells[4] = {1, 3, 6, 9};
    for (int i = 0; i < 4; ++i) {
        const int anchorN = set.shapes[static_cast<size_t>(i)].anchorN;
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "level index " << i;
        const auto cells = map.shapeCells(set.levels[static_cast<size_t>(i)], anchor);
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, expectedCells[i]) << "level index " << i;
    }
}

TEST(TilingTable, Arch3464CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "arch_3464";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch3464))];
    ASSERT_EQ(set.levels.size(), 4u);
    const int expectedCells[4] = {1, 3, 7, 13};
    for (int i = 0; i < 4; ++i) {
        const int anchorN = set.shapes[static_cast<size_t>(i)].anchorN;
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "level index " << i;
        const auto cells = map.shapeCells(set.levels[static_cast<size_t>(i)], anchor);
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, expectedCells[i]) << "level index " << i;
    }
}

TEST(TilingTable, Arch4612CityShapesResolveToExpectedCellCountsWithVariants) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "arch_4612";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch4612))];
    ASSERT_EQ(set.levels.size(), 5u);
    ASSERT_EQ(set.shapes.size(), 6u);  // L2 有两个变体
    for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
        const double level = set.levels[static_cast<size_t>(set.shapeLevelIndex[static_cast<size_t>(s)])];
        const int vc = set.variantCount(level);
        const int anchorN = set.shapes[static_cast<size_t>(s)].anchorN;
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "shape index " << s;
        const int variant = (vc > 1) ? (std::abs(anchor) % vc) : 0;
        const int shapeIdx = set.firstShapeIndex(level) + variant;
        const auto cells = map.shapeCells(level, anchor);
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, static_cast<int>(set.shapes[static_cast<size_t>(shapeIdx)].cells.size()))
            << "shape index " << s << " variant " << variant;
    }
}

TEST(TilingTable, Arch31212CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "arch_31212";
    mc.width = 6;
    mc.height = 6;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch31212))];
    ASSERT_EQ(set.levels.size(), 5u);
    const int expectedCells[5] = {3, 7, 4, 7, 16};
    for (int i = 0; i < 5; ++i) {
        const int anchorN = set.shapes[static_cast<size_t>(i)].anchorN;
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (map.geom().neighborCount(idx) == anchorN) {
                double cx, cy;
                map.geom().cellCenter(idx, cx, cy);
                if (cx > 0.2 * map.worldWidth() && cx < 0.8 * map.worldWidth()
                    && cy > 0.2 * map.worldHeight() && cy < 0.8 * map.worldHeight()) {
                    anchor = idx;
                    break;
                }
            }
        }
        ASSERT_GE(anchor, 0) << "level index " << i;
        const auto cells = map.shapeCells(set.levels[static_cast<size_t>(i)], anchor);
        int count = 0;
        for (int c : cells)
            if (c >= 0) ++count;
        EXPECT_EQ(count, expectedCells[i]) << "level index " << i;
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
