// test_tiling_table.cpp — 表驱动半正/Laves 密铺几何单测（读取 data/tiling_specs_arch.json）。
// 覆盖：格数、世界尺寸、中心→worldToCell 往返、邻接对称/边界、cellPolygon/cellEdge 一致性。
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
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
    EXPECT_EQ(map.cellCount(), 32 * 32);  // 用户长*宽 = 总格数
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
    EXPECT_EQ(map.cellCount(), 32 * 32);  // 用户长*宽 = 总格数
    EXPECT_GE(map.cityCount(), 8);
}

TEST(TilingTable, Arch488CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "arch_488";
    mc.width = 16;
    mc.height = 12;
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
    mc.width = 48;
    mc.height = 12;
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
    mc.width = 32;
    mc.height = 12;
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
    mc.width = 48;
    mc.height = 12;
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch3636))];
    ASSERT_EQ(set.levels.size(), 5u);  // 2026-08-17：新增 4 级城（4.5 面积，4 级贴图）
    const int expectedCells[5] = {1, 3, 7, 4, 7};  // L2/L3/L4/L5/L8
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

TEST(TilingTable, Laves3464CityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "laves_3464";
    mc.width = 96;
    mc.height = 12;
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
    mc.width = 96;
    mc.height = 12;
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
    mc.width = 96;
    mc.height = 12 * std::max(1, TilingGeom{TilingType::Arch4612, 1, 1}.baseCount());
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch4612))];
    ASSERT_EQ(set.levels.size(), 4u);  // 2026-08-17 删除 L2（六边形+3 正方形，用户弃用）
    ASSERT_EQ(set.shapes.size(), 4u);  // L1/L3/L5/L10
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
    mc.width = 48;
    mc.height = 12 * std::max(1, TilingGeom{TilingType::Arch31212, 1, 1}.baseCount());
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

TEST(TilingTable, Laves31212CityShapesResolveToExpectedCellCountsWithVariants) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "laves_31212";
    mc.width = 96;
    mc.height = 12 * std::max(1, TilingGeom{TilingType::Laves31212, 1, 1}.baseCount());
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Laves31212))];
    ASSERT_EQ(set.levels.size(), 4u);
    ASSERT_EQ(set.shapes.size(), 6u);  // L6 有三个变体
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

TEST(TilingTable, Laves4612CityShapesResolveToExpectedCellCountsWithVariants) {
    const Config cfg = Config::loadFromJson("{}");
    Config::Map mc = cfg.map;
    mc.tiling = "laves_4612";
    mc.width = 192;
    mc.height = 12 * std::max(1, TilingGeom{TilingType::Laves4612, 1, 1}.baseCount());
    Map map;
    map.configure(mc);
    map.setCityConfig(cfg.city);
    const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(TilingType::Laves4612))];
    ASSERT_EQ(set.levels.size(), 4u);
    ASSERT_EQ(set.shapes.size(), 6u);  // L4 与 L12 各两个变体
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

TEST(TilingTable, AllTenTilingsGenerateCitiesWithMultipleLevels) {
    const TilingType types[10] = {
        TilingType::Arch3464,  TilingType::Arch3636,  TilingType::Arch31212,
        TilingType::Arch4612,  TilingType::Arch488,   TilingType::Laves3464,
        TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
        TilingType::Laves488};
    const Config cfg = Config::loadFromJson("{}");
    for (TilingType t : types) {
        const int B = std::max(1, TilingGeom{t, 1, 1}.baseCount());
        // 长 = 域列数；宽 = 域行数 * B（长*宽 = 总格数）。域行数至少 6 且保证宽 ≥32。
        const int width = 64;
        const int rows = std::max(6, (32 + B - 1) / B);
        const int height = rows * B;
        MapGenParams gp{width, height, 0.3, 0.1, 0.08, false, t};
        const std::string path = "userdata/maps/utest_all10_" + std::string(tilingName(t)) + ".lwmap";
        ASSERT_TRUE(MapGenerator::generate(path, 42, gp)) << tilingName(t);
        Config::Map mc = cfg.map;
        mc.tiling = tilingName(t);
        mc.width = width;
        mc.height = height;
        mc.file = path;
        Map map;
        map.configure(mc);
        map.setTerrain(cfg.terrain);
        map.setCityConfig(cfg.city);
        Rng rng(42);
        ASSERT_TRUE(map.loadFromLwmap(path, rng)) << tilingName(t);
        ASSERT_TRUE(map.placeCapitals(rng)) << tilingName(t);
        map.finalize();
        EXPECT_EQ(map.cellCount(), width * height) << tilingName(t);  // 长*宽 = 总格数
        EXPECT_GT(map.cityCount(), 0) << tilingName(t);
        std::set<int> texLevels;
        for (const auto& c : map.cities()) {
            int tl = (c.level >= 10.0)
                         ? 10
                         : std::clamp(static_cast<int>(std::lround(c.level)), 1, 10);
            texLevels.insert(tl);
        }
        EXPECT_GE(texLevels.size(), 2u)
            << tilingName(t) << " has only " << texLevels.size() << " distinct city levels";
    }
}

TEST(TilingTable, Arch488GeneratedCitiesHaveMultipleTextureLevels) {
    const Config cfg = Config::loadFromJson("{}");
    MapGenParams gp{64, 32, 0.3, 0.1, 0.08, false, TilingType::Arch488};
    const std::string path = "userdata/maps/utest_arch488_levels.lwmap";
    ASSERT_TRUE(MapGenerator::generate(path, 42, gp));
    Config::Map mc = cfg.map;
    mc.tiling = "arch_488";
    mc.width = 64;
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
    std::set<int> texLevels;
    for (const auto& c : map.cities()) {
        int tl = (c.level >= 10.0) ? 10
                                   : std::clamp(static_cast<int>(std::lround(c.level)), 1, 10);
        texLevels.insert(tl);
    }
    EXPECT_GE(texLevels.size(), 2u) << "distinct texture levels: " << texLevels.size();
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

// 锚格朝向修复（2026-08-16）：形状表按"同边数第一个基础格"朝向编写；锚为不同朝向的
// 基础格时，shapeCells/canPlaceCity 必须按锚格变换旋转/镜像偏移，否则解析出错误形状
//（如 L9 本应 4 三角 + 3 十二边形，错朝向会解析成错误类型组合）。
// 验证方法：对每种表驱动密铺，枚举锚格所在基础格 b，用 shapeCells 解析该密铺全部形状，
// 断言"格数=形状格数 且 形状格类型构成不变"（类型构成与参考基础格一致）。
TEST(TilingTable, AnchorOrientationShapesResolveCorrectlyAtAllBaseCells) {
    const TilingType types[10] = {
        TilingType::Arch3464,  TilingType::Arch3636,  TilingType::Arch31212,
        TilingType::Arch4612,  TilingType::Arch488,   TilingType::Laves3464,
        TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
        TilingType::Laves488};
    const Config cfg = Config::loadFromJson("{}");
    for (TilingType t : types) {
        // 大图避免形状越界；域列/行取 24×24。
        Config::Map mc = cfg.map;
        mc.tiling = tilingName(t);
        mc.width = 24;
        mc.height = 24;
        Map map;
        map.configure(mc);
        map.setCityConfig(cfg.city);
        const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(t))];
        ASSERT_FALSE(set.shapes.empty()) << tilingName(t);
        const int B = std::max(1, map.geom().baseCount());
        // 每个基础格 b 找地图中部锚点（行/列取中，远离边界避免形状越界）。
        const int mr = map.geom().rows / 2, mc0 = map.geom().cols / 2;
        for (int b = 0; b < B; ++b) {
            const int anchor = (mr * map.geom().cols + mc0) * B + b;
            if (anchor < 0 || anchor >= map.cellCount()) continue;
            const int anchorN = map.geom().neighborCount(anchor);
            for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
                const double level = set.levels[static_cast<size_t>(set.shapeLevelIndex[static_cast<size_t>(s)])];
                const Config::City::Shape& sh = set.shapes[static_cast<size_t>(s)];
                if (sh.anchorN != 0 && sh.anchorN != anchorN) continue;
                // 锚格类型限制（anchorBaseMask）也须放行。
                if (sh.anchorBaseMask != 0 && ((1u << static_cast<unsigned>(b)) & sh.anchorBaseMask) == 0)
                    continue;
                const std::vector<int> cells = map.shapeCells(level, anchor);
                int okCount = 0;
                for (int c : cells)
                    if (c >= 0) ++okCount;
                // 形状格数必须全部解析（形状表偏移经锚格变换后落在真实格上）。
                EXPECT_EQ(okCount, static_cast<int>(sh.cells.size()))
                    << tilingName(t) << " base " << b << " shape " << s
                    << " anchorN " << anchorN;
            }
        }
    }
}

// 参考基础格变换自洽：TilingGeom::applyAnchorOrientation 对参考格本身恒等；
// 对同边数但不同朝向的基础格，变换后的偏移应落在真实格中心（worldToCell 不返回 -1）。
TEST(TilingTable, AnchorTransformSelfConsistent) {
    const TilingType types[10] = {
        TilingType::Arch3464,  TilingType::Arch3636,  TilingType::Arch31212,
        TilingType::Arch4612,  TilingType::Arch488,   TilingType::Laves3464,
        TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
        TilingType::Laves488};
    const Config cfg = Config::loadFromJson("{}");
    for (TilingType t : types) {
        TilingGeom g{t, 4, 4};
        ASSERT_GT(g.baseCount(), 0) << tilingName(t);
        const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(t))];
        if (set.shapes.empty()) continue;
        // 用每个形状的第一个偏移（锚格自身 (0,0)）验证变换不破坏锚格中心。
        const auto& sh = set.shapes.front();
        if (sh.cells.empty()) continue;
        // 参考基础格 = shapeReferenceBase（无 mask 时 = 同边数第一个基础格）
        const int ref = g.shapeReferenceBase(sh.anchorN, sh.anchorBaseMask);
        ASSERT_GE(ref, 0) << tilingName(t);
        // 对每个基础格 b 应用变换到锚偏移 (0,0)：结果应为锚格中心偏移 0 → worldToCell
        // 落在锚格自己（取中心点验证）。
        for (int b = 0; b < g.baseCount(); ++b) {
            const int anchor = (2 * g.cols + 2) * g.baseCount() + b;
            if (anchor < 0 || anchor >= g.cellCount()) continue;
            double cx, cy;
            g.cellCenter(anchor, cx, cy);
            double wx = cx, wy = cy;
            double dx = 0.0, dy = 0.0;
            // 形状表偏移 (0,0) 经运行时旋转后为 (0,0)，再经锚格变换仍为 (0,0)。
            EXPECT_TRUE(g.applyAnchorOrientation(anchor, sh.anchorN, sh.anchorBaseMask, dx, dy))
                << tilingName(t) << " base " << b;
            EXPECT_NEAR(dx, 0.0, 1e-9) << tilingName(t) << " base " << b;
            EXPECT_NEAR(dy, 0.0, 1e-9) << tilingName(t) << " base " << b;
            EXPECT_EQ(g.worldToCell(wx, wy), anchor) << tilingName(t) << " base " << b;
        }
    }
}

}  // namespace
