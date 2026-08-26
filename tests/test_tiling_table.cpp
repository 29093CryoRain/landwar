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

// 锚点基础格位掩码 → 参考基础格（最低置位；无限制 → 0）。形状锚朝向已由数据保证、
// 不再有 TilingGeom::shapeReferenceBase，测试就地取最低位作确定性参考锚。
int referenceBase(std::uint32_t mask) {
    for (int b = 0; b < 31; ++b)
        if (mask & (1u << static_cast<unsigned>(b))) return b;
    return 0;
}

// 形状在某参考基础格的众多候选锚点中，能解析出的最大格数（跨多个中部锚扫描）。
// 形状表是"世界帧偏移 + 锚基础格限制"：只要某个合法锚能让全部偏移落到界内格（= 全解析、
// 无退化），就证明该形状非畸形。扫描取最大值，规避"测试恰落在不可用锚"的假阴性。
int maxResolveCount(const lw::Map& map, double level, int refB, int variant) {
    int best = 0;
    const int B = std::max(1, map.geom().baseCount());
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        if (idx % B != refB) continue;
        double cx, cy;
        map.geom().cellCenter(idx, cx, cy);
        if (cx < 0.15 * map.worldWidth() || cx > 0.85 * map.worldWidth() ||
            cy < 0.15 * map.worldHeight() || cy > 0.85 * map.worldHeight())
            continue;
        const auto cells = map.shapeCells(level, idx, variant);
        int cnt = 0;
        for (int c : cells)
            if (c >= 0) ++cnt;
        best = std::max(best, cnt);
    }
    return best;
}

const TilingType kTableTypes[] = {
    TilingType::Arch3464,  TilingType::Arch3636,  TilingType::Arch31212,
    TilingType::Arch4612,  TilingType::Arch488,   TilingType::Laves3464,
    TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
    TilingType::Laves488,  TilingType::Arch33434, TilingType::Arch33336,
    TilingType::Laves33434, TilingType::Laves33336};

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

TEST(TilingTable, SnubTilingsGenerateLoadAndCellRoundTrip) {
    // 2026-08-25：扭棱类四密铺方向纠偏后的几何/生成验证（arch_33434 轴对齐、arch/laves_33336
    // 平行四边形周期域）。生成+加载+worldToCell 全格往返 + 首都放置（现已有城市形状表）。
    const std::pair<int, int> lim[4] = {{16, 15}, {3, 6}, {3, 8}, {8, 9}};  // (Ra,Rb)，2026-08 重算
    const TilingType types[4] = {TilingType::Arch33434, TilingType::Arch33336,
                                 TilingType::Laves33434, TilingType::Laves33336};
    const Config cfg = Config::loadFromJson("{}");
    for (int i = 0; i < 4; ++i) {
        const TilingType t = types[i];
        const int B = std::max(1, TilingGeom{t, 1, 1}.baseCount());
        const int Ra = lim[i].first, Rb = lim[i].second;
        const int width = std::max(24, ((54 + Ra - 1) / Ra) * Ra);
        const int height = std::max(10, ((40 + Rb - 1) / Rb) * Rb);
        int tc, tr;
        chooseTableDomain(static_cast<int>(t), width, height, tc, tr);
        const int expectCells = B * tc * tr;
        MapGenParams gp{width, height, 0.35, 0.1, 0.06, 0.3, false, t};
        const std::string path = "userdata/maps/utest_snub_" + std::string(tilingName(t)) + ".lwmap";
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
        map.finalize();
        EXPECT_EQ(map.cellCount(), expectCells) << tilingName(t);
        // 可玩性：应能放置首都（半正/Laves 从已生成城市分配；多格形状需能解析）。
        ASSERT_TRUE(map.placeCapitals(rng)) << tilingName(t);
        EXPECT_GE(map.cityCount(), 8) << tilingName(t);
        // 2026-08 斜周期地形采样修正：按格坐标（常规矩形）采样 → 陆地占比应近似 seaRatio
        //（阈值分位数机制基本精确；允许少量偏差 = 外圈值/山城骰无关，纯海陆计数）。
        if (map.geom().hasSkewedPeriod()) {
            long landCells = 0;
            for (int idx = 0; idx < map.cellCount(); ++idx)
                if (map.atIndex(idx).land) ++landCells;
            const double frac = static_cast<double>(landCells) / map.cellCount();
            EXPECT_GT(frac, 0.30) << tilingName(t) << " land fraction " << frac;
            EXPECT_LT(frac, 0.80) << tilingName(t) << " land fraction " << frac;
        }
        // 全格 worldToCell(cellCenter) 往返（几何正确性核心）。
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            double cx, cy;
            map.geom().cellCenter(idx, cx, cy);
            EXPECT_EQ(map.geom().worldToCell(cx, cy), idx)
                << tilingName(t) << " idx=" << idx;
            // 2026-08 斜周期：世界坐标系已平移归一到 [0,worldWidth]x[0,worldHeight]
            //（相机/兵特效/空间哈希夹取均假定此域），故所有格中心应落在该域内。
            EXPECT_GE(cx, -1e-6) << tilingName(t) << " idx=" << idx << " cx=" << cx;
            EXPECT_GE(cy, -1e-6) << tilingName(t) << " idx=" << idx << " cy=" << cy;
            EXPECT_LE(cx, map.worldWidth() + 1e-6) << tilingName(t) << " idx=" << idx;
            EXPECT_LE(cy, map.worldHeight() + 1e-6) << tilingName(t) << " idx=" << idx;
        }
    }
}

// 扭棱类四密铺城市形状表：各形状在中部锚格处解析出的格数 = 期望（.docs/异种地图开发思路.txt）。
TEST(TilingTable, SnubCityShapesResolveToExpectedCellCounts) {
    const Config cfg = Config::loadFromJson("{}");
    struct Case {
        TilingType t;
        int width, height;                    // 地图尺寸（输入长/宽）
        std::vector<double> levels;           // 等级
        std::vector<int> counts;              // 每等级形状格数（无变体时逐等级）
        int baseMult = 1;                     // 宽方向基础格倍数（laves33336 等大 B 密铺）
    };
    const Case cases[] = {
        {TilingType::Arch33336, 96, 12, {9.0 / 7.0, 18.0 / 7.0, 27.0 / 7.0, 36.0 / 7.0, 9.0},
         {2, 4, 1, 3, 9}},
        {TilingType::Arch33434, 96, 12,
         {0.6961524227066321, 1.6076951545867362, 5.303847577293366, 12.0}, {1, 1, 5, 5, 12, 12}},
        {TilingType::Laves33336, 64, 12, {1.0, 3.0, 6.0}, {1, 3, 3, 6}},
        {TilingType::Laves33434, 64, 12, {1.0, 4.0, 12.0}, {1, 4, 4, 12, 12}},
    };
    for (const Case& cs : cases) {
        Config::Map mc = cfg.map;
        mc.tiling = tilingName(cs.t);
        mc.width = cs.width;
        mc.height = cs.height;
        Map map;
        map.configure(mc);
        map.setCityConfig(cfg.city);
        const auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(cs.t))];
        // 每个形状都应能完整解析（= 其定义格数）——只验证"非畸形"，具体格数随多变体数据而定。
        for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
            const std::uint32_t baseMask = set.shapes[static_cast<size_t>(s)].anchorBaseMask;
            const int refB = referenceBase(baseMask);
            const double level =
                set.levels[static_cast<size_t>(set.shapeLevelIndex[static_cast<size_t>(s)])];
            const int expected = static_cast<int>(set.shapes[static_cast<size_t>(s)].cells.size());
            const int variant = s - set.firstShapeIndex(level);
            const int got = maxResolveCount(map, level, refB, variant);
            EXPECT_EQ(got, expected)
                << tilingName(cs.t) << " shape " << s << " level " << level;
        }
    }
}

TEST(TilingTable, Arch488MapGenerationAndCapitalPlacement) {
    const Config cfg = Config::loadFromJson("{}");
    // 2026-08 比例保持映射：arch_488 的输入长/宽须为 Ra=7、Rb=10 的倍数（见 Tiling.cpp
    // tableDomainParams），否则会被向上取整。此处传合规值，使命中"长*宽=总格数"精确成立。
    MapGenParams gp{84, 120, 0.4, 0.1, 0.02, 0.3, false, TilingType::Arch488};
    const std::string path = "userdata/maps/utest_arch488.lwmap";
    ASSERT_TRUE(MapGenerator::generate(path, 42, gp));
    Config::Map mc = cfg.map;
    mc.tiling = "arch_488";
    mc.width = 84;
    mc.height = 120;
    mc.file = path;
    Map map;
    map.configure(mc);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    Rng rng(42);
    ASSERT_TRUE(map.loadFromLwmap(path, rng));
    ASSERT_TRUE(map.placeCapitals(rng));
    map.finalize();
    EXPECT_EQ(map.cellCount(), 84 * 120);  // 用户长*宽 = 总格数（合规输入）
    EXPECT_GE(map.cityCount(), 8);  // 至少 8 个首都城
}

TEST(TilingTable, Laves488MapGenerationAndCapitalPlacement) {
    const Config cfg = Config::loadFromJson("{}");
    MapGenParams gp{32, 32, 0.4, 0.1, 0.02, 0.3, false, TilingType::Laves488};
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
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(i)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (idx % std::max(1, map.geom().baseCount()) == refB) {
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
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(i)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (idx % std::max(1, map.geom().baseCount()) == refB) {
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
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(i)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (idx % std::max(1, map.geom().baseCount()) == refB) {
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
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(i)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (idx % std::max(1, map.geom().baseCount()) == refB) {
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
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(i)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (idx % std::max(1, map.geom().baseCount()) == refB) {
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
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(i)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (idx % std::max(1, map.geom().baseCount()) == refB) {
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
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(s)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        int anchor = -1;
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            if (idx % std::max(1, map.geom().baseCount()) == refB) {
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
    // arch_31212 每级至少一个形状（级别一变体），逐个验证非畸形（可完整解析）。
    for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(s)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        const double level =
            set.levels[static_cast<size_t>(set.shapeLevelIndex[static_cast<size_t>(s)])];
        const int expected = static_cast<int>(set.shapes[static_cast<size_t>(s)].cells.size());
        const int variant = s - set.firstShapeIndex(level);
        const int got = maxResolveCount(map, level, refB, variant);
        EXPECT_EQ(got, expected)
            << "shape index " << s << " level " << level;
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
    ASSERT_EQ(set.shapes.size(), 7u);  // L6 有三个变体
    for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
        const double level = set.levels[static_cast<size_t>(set.shapeLevelIndex[static_cast<size_t>(s)])];
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(s)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        const int expected = static_cast<int>(set.shapes[static_cast<size_t>(s)].cells.size());
        const int variant = s - set.firstShapeIndex(level);
        EXPECT_EQ(maxResolveCount(map, level, refB, variant), expected)
            << "shape index " << s << " level " << level;
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
    ASSERT_EQ(set.shapes.size(), 11u);  // L4 三个变体、L6 四个变体、L12 三个变体
    EXPECT_EQ(set.variantCount(6.0), 4);
    // L6 变体 = shapes 中 levelIndex==2 的形状（levelIndex 2 = levels[2]=6.0）。
    std::vector<int> l6shapes;
    for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s)
        if (set.shapeLevelIndex[static_cast<size_t>(s)] == 2) l6shapes.push_back(s);
    ASSERT_EQ(l6shapes.size(), 4u);
    // 指纹 = 各格 (dx,dy) 的汇总（ShapeCell 无 ==，用偏移区分变体）。
    const auto fp = [&](int s) {
        double sumx = 0, sumy = 0;
        for (const auto& c : set.shapes[static_cast<size_t>(s)].cells) {
            sumx += c.dx;
            sumy += c.dy;
        }
        return sumx * 1e3 + sumy * 1e3;
    };
    ASSERT_GT(l6shapes.size(), 1u);
    EXPECT_NE(fp(l6shapes[0]), fp(l6shapes[1])) << "L6 各变体应互不相同";
    for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
        const double level = set.levels[static_cast<size_t>(set.shapeLevelIndex[static_cast<size_t>(s)])];
        const std::uint32_t baseMask = set.shapes[static_cast<size_t>(s)].anchorBaseMask;
        const int refB = referenceBase(baseMask);
        const int expected = static_cast<int>(set.shapes[static_cast<size_t>(s)].cells.size());
        const int variant = s - set.firstShapeIndex(level);
        EXPECT_EQ(maxResolveCount(map, level, refB, variant), expected)
            << "shape index " << s << " level " << level;
    }
}

TEST(TilingTable, AllTenTilingsGenerateCitiesWithMultipleLevels) {
    const TilingType types[14] = {
        TilingType::Arch3464,  TilingType::Arch3636,  TilingType::Arch31212,
        TilingType::Arch4612,  TilingType::Arch488,   TilingType::Laves3464,
        TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
        TilingType::Laves488,  TilingType::Arch33336, TilingType::Arch33434,
        TilingType::Laves33336, TilingType::Laves33434};
    // 每密铺的输入限制 (Ra,Rb)，与 Tiling.cpp 的 tableDomainParams 保持一致（算法一离线求得）。
    // 传合规的长/宽（Ra/Rb 的倍数）→ 命中"长*宽=总格数"精确成立（否则 chooseTableDomain 会
    // 向上取整，cellCount 为取整后的现值）。
    const std::pair<int, int> lim[14] = {
        {8, 9}, {15, 16}, {15, 16}, {9, 8}, {7, 10},   // Arch3464/3636/31212/4612/488
        {8, 9}, {15, 16}, {8, 9}, {4, 6}, {2, 2},      // Laves3464/3636/31212/4612/488
        {3, 6}, {16, 15}, {8, 9}, {8, 3}};             // Arch33336/33434、Laves33336/33434
    const Config cfg = Config::loadFromJson("{}");
    for (int i = 0; i < 14; ++i) {
        TilingType t = types[i];
        const int B = std::max(1, TilingGeom{t, 1, 1}.baseCount());
        const int Ra = lim[i].first, Rb = lim[i].second;
        // 长 = Ra 倍数（≥64）；宽 = Rb 倍数（≥ ~6 域行 × B，保证城市多样）。
        const int width = std::max(64, ((64 + Ra - 1) / Ra) * Ra);
        const int height = std::max(6, (32 + B - 1) / B) * B;
        const int hc = ((height + Rb - 1) / Rb) * Rb;  // 宽向上取整到 Rb 倍数
        // 守恒预期：chooseTableDomain 会把 (width,hc) 合规后映射出 cols,rows；cellCount 应恰为
        // B*cols*rows（= 合规后的有效长*宽）。
        int tc, tr;
        chooseTableDomain(static_cast<int>(t), width, hc, tc, tr);
        const int expectCells = B * tc * tr;
        MapGenParams gp{width, hc, 0.3, 0.1, 0.08, 0.3, false, t};
        const std::string path = "userdata/maps/utest_all10_" + std::string(tilingName(t)) + ".lwmap";
        ASSERT_TRUE(MapGenerator::generate(path, 42, gp)) << tilingName(t);
        Config::Map mc = cfg.map;
        mc.tiling = tilingName(t);
        mc.width = width;
        mc.height = hc;
        mc.file = path;
        Map map;
        map.configure(mc);
        map.setTerrain(cfg.terrain);
        map.setCityConfig(cfg.city);
        Rng rng(42);
        ASSERT_TRUE(map.loadFromLwmap(path, rng)) << tilingName(t);
        ASSERT_TRUE(map.placeCapitals(rng)) << tilingName(t);
        map.finalize();
        EXPECT_EQ(map.cellCount(), expectCells) << tilingName(t);  // 守恒：cellCount = B*cols*rows
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
    MapGenParams gp{63, 40, 0.3, 0.1, 0.08, 0.3, false, TilingType::Arch488};
    const std::string path = "userdata/maps/utest_arch488_levels.lwmap";
    ASSERT_TRUE(MapGenerator::generate(path, 42, gp));
    Config::Map mc = cfg.map;
    mc.tiling = "arch_488";
    mc.width = 63;
    mc.height = 40;
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
    const TilingType types[14] = {
        TilingType::Arch3464,  TilingType::Arch3636,  TilingType::Arch31212,
        TilingType::Arch4612,  TilingType::Arch488,   TilingType::Laves3464,
        TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
        TilingType::Laves488,  TilingType::Arch33336, TilingType::Arch33434,
        TilingType::Laves33336, TilingType::Laves33434};
    const Config cfg = Config::loadFromJson("{}");
    for (TilingType t : types) {
        // 大图避免形状越界；域列/行取足够大。高 B 密铺（如 laves_4612 B=24）rows=宽/B，
        // 须给足"宽"使最坏城市形状（跨 ~2-3 个周期域）能放下：取 宽 ≥ 8*B、长 ≥ 16。
        Config::Map mc = cfg.map;
        mc.tiling = tilingName(t);
        mc.width = 32;
        mc.height = std::max(64, 8 * std::max(1, TilingGeom{t, 1, 1}.baseCount()));
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
            for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
                const double level = set.levels[static_cast<size_t>(set.shapeLevelIndex[static_cast<size_t>(s)])];
                const Config::City::Shape& sh = set.shapes[static_cast<size_t>(s)];
                // 锚点限制（anchorBaseMask）须放行；无限制形状仅在参考朝向（b=0）验证。
                if (sh.anchorBaseMask != 0 && ((1u << static_cast<unsigned>(b)) & sh.anchorBaseMask) == 0)
                    continue;
                if (sh.anchorBaseMask == 0 && b != 0) continue;
                const std::vector<int> cells = map.shapeCells(level, anchor);
                int okCount = 0;
                for (int c : cells)
                    if (c >= 0) ++okCount;
                // 形状格数必须全部解析（形状表偏移经锚格变换后落在真实格上）。
                EXPECT_EQ(okCount, static_cast<int>(sh.cells.size()))
                    << tilingName(t) << " base " << b << " shape " << s;
            }
        }
    }
}

// 地块双色分档（2026-08 异种地图开发思路：「与双色渲染系统」）：
//   方/六不分档（tilePaletteSize==0）；三角按正/反朝向分两档（同 laves 分档）；
//   arch 按边数升序分档（档数=唯一边数）；laves 按朝向（旋转+镜像）分档
//   （档数=朝向种类数 G 的最小质因子 p，循环）。
TEST(TilingTable, TileColorPalette) {
    auto pal = [](TilingType tp) { return TilingGeom{tp, 4, 3}.tilePaletteSize(); };
    // 方（单色）/六（单朝向）：不分档；三角：正/反两朝向 → 2 档。
    EXPECT_EQ(pal(TilingType::Square), 0);
    EXPECT_EQ(pal(TilingType::Hex), 0);
    EXPECT_EQ(pal(TilingType::Tri), 2);
    // arch：唯一边数 = 档数。
    EXPECT_EQ(pal(TilingType::Arch3636), 2);   // 三角+六边
    EXPECT_EQ(pal(TilingType::Arch3464), 3);   // 三角+方+六
    EXPECT_EQ(pal(TilingType::Arch31212), 2);  // 三角+十二边
    EXPECT_EQ(pal(TilingType::Arch488), 2);    // 方+八边
}

TEST(TilingTable, TileColorIndexArchBySideCountAscending) {
    // arch：格档位 = 其边数在唯一边数升序中的位置（小边数 → 档 0）。
    for (TilingType t : {TilingType::Arch3636, TilingType::Arch3464, TilingType::Arch31212,
                         TilingType::Arch488}) {
        TilingGeom g{t, 4, 3};
        const int pal = g.tilePaletteSize();
        ASSERT_GT(pal, 0) << tilingName(t);
        std::set<int> sides;
        for (int b = 0; b < g.baseCount(); ++b) sides.insert(g.neighborCount(b));
        ASSERT_EQ(static_cast<int>(sides.size()), pal) << tilingName(t);
        // 每档至少出现一次，且同边数格同档。
        std::vector<int> seen(static_cast<size_t>(pal), 0);
        for (int b = 0; b < g.baseCount(); ++b) {
            const int idx = g.cellIndexAt(0, 0, b);
            const int n = g.neighborCount(idx);
            const int expected =
                static_cast<int>(std::distance(sides.begin(), sides.lower_bound(n)));
            EXPECT_EQ(g.tileColorIndex(idx), expected) << tilingName(t) << " base " << b;
            ++seen[static_cast<size_t>(expected)];
        }
        for (int s : seen) EXPECT_GT(s, 0) << tilingName(t);  // 排除空档
    }
}

TEST(TilingTable, TileColorIndexLavesByOrientationCycle) {
    // laves：档数 = 朝向种类数 G 的最小质因子 p；每格档位 ∈ [0,p)。
    const std::vector<TilingType> laves = {TilingType::Laves3636, TilingType::Laves31212,
                                           TilingType::Laves4612, TilingType::Laves488,
                                           TilingType::Laves33434, TilingType::Laves3464};
    for (TilingType t : laves) {
        TilingGeom g{t, 4, 3};
        const int pal = g.tilePaletteSize();
        ASSERT_GT(pal, 0) << tilingName(t);
        // pal = 朝向种类数 G 的最小质因子 → 素数（G 为素数）或 ≥2 的合数最小素因子；恒 ≥2。
        EXPECT_GE(pal, 2) << tilingName(t);
        // 每基础格档位合法且在范围内。
        for (int b = 0; b < g.baseCount(); ++b) {
            const int idx = g.cellIndexAt(0, 0, b);
            const int ci = g.tileColorIndex(idx);
            EXPECT_GE(ci, 0) << tilingName(t) << " base " << b;
            EXPECT_LT(ci, pal) << tilingName(t) << " base " << b;
        }
    }
    // 菱形（laves_3636）：朝向 3 种 → p=3 → 3 档（端+中）。
    const TilingGeom rhombus{TilingType::Laves3636, 4, 3};
    EXPECT_EQ(rhombus.tilePaletteSize(), 3);
}

TEST(TilingTable, TileColorIndexTriUpDown) {
    // 三角：正（偶下标 = 顶点朝上）与反（奇下标 = 顶点朝下）各一档，档位 = index & 1。
    const TilingGeom g{TilingType::Tri, 10, 8};
    EXPECT_EQ(g.tilePaletteSize(), 2);
    for (int idx = 0; idx < g.cellCount(); ++idx)
        EXPECT_EQ(g.tileColorIndex(idx), idx & 1) << "idx=" << idx;
}

}  // namespace
