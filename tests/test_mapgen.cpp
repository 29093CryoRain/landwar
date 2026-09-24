#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "core/Config.h"
#include "world/Map.h"
#include "world/MapGenerator.h"
#include "world/RiverGenerator.h"
#include "TestUtil.h"

namespace {

TEST(MapGen, SameSeedProducesIdenticalNativeDefinition) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params{48, 40, 0.35, 0.08, 0.03};
    lw::MapDefinition a, b;
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, a, cfg.city));
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, b, cfg.city));
    EXPECT_EQ(a.toJson(), b.toJson());
    const std::string defaultPath = lw::MapGenerator::defaultPath(42, params);
    EXPECT_TRUE(defaultPath.starts_with(std::string(lw::kMapDataDir) + "/"));
    EXPECT_TRUE(defaultPath.ends_with(".landmap"));
}

TEST(MapGen, DifferentSeedChangesNativeDefinition) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params{48, 40, 0.35, 0.08, 0.03};
    lw::MapDefinition a, b;
    ASSERT_TRUE(lw::MapGenerator::generate(1, params, a, cfg.city));
    ASSERT_TRUE(lw::MapGenerator::generate(2, params, b, cfg.city));
    EXPECT_NE(a.toJson(), b.toJson());
}

TEST(MapGen, ExplicitExportRoundTripsNativeDefinition) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params{48, 40, 0.0, 0.05, 0.02};
    lw::MapDefinition generated;
    ASSERT_TRUE(lw::MapGenerator::generate(7, params, generated, cfg.city));
    const std::string path = lwtest::testArtifactPath("generated.landmap");
    ASSERT_TRUE(lw::MapGenerator::generate(path, 7, params));
    lw::MapDefinition loaded;
    std::string error;
    ASSERT_TRUE(lw::MapDefinition::loadFromFile(path, loaded, &error)) << error;
    EXPECT_EQ(loaded.toJson(), generated.toJson());
    std::remove(path.c_str());
}

TEST(MapGen, GeneratedDefinitionLoadsWithExactTerrain) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params{48, 40, 0.0, 0.15, 0.03};
    lw::MapDefinition definition;
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, definition, cfg.city));
    lw::Config::Map mapConfig = cfg.map;
    mapConfig.width = definition.cols;
    mapConfig.height = definition.rows;
    mapConfig.tiling = lw::tilingName(definition.tiling);
    lw::Map map;
    map.configure(mapConfig);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    ASSERT_TRUE(map.loadFromDefinition(definition));
    ASSERT_EQ(map.cellCount(), static_cast<int>(definition.terrain.size()));
    ASSERT_EQ(map.cityCount(), static_cast<int>(definition.cities.size()));
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        const auto terrain = definition.terrain[static_cast<std::size_t>(idx)];
        EXPECT_EQ(map.atIndex(idx).land, terrain != lw::MapTerrain::Sea);
        EXPECT_EQ(map.atIndex(idx).mountain, terrain == lw::MapTerrain::Mountain);
    }
}

TEST(MapGen, SkewedPeriodsApplyGradualCoastFalloffInsideHardBoundary) {
    const lw::Config cfg = lwtest::loadCfg();
    for (lw::TilingType type : {lw::TilingType::Arch33336, lw::TilingType::Laves33336}) {
        lw::MapGenParams withoutFalloff;
        withoutFalloff.width = 120;
        withoutFalloff.height = 120;
        withoutFalloff.seaRatio = 0.35;
        withoutFalloff.mountainDensity = 0.0;
        withoutFalloff.cityDensity = 0.0;
        withoutFalloff.forceCoast = true;
        withoutFalloff.tiling = type;
        withoutFalloff.forceCoastRangeMultiplier = 0.0;

        lw::MapGenParams withFalloff = withoutFalloff;
        withFalloff.forceCoastRangeMultiplier = 1.0;
        withFalloff.forceCoastStrengthMultiplier = 1.0;

        lw::MapDefinition hardBoundaryOnly, gradualCoast;
        ASSERT_TRUE(lw::MapGenerator::generate(42, withoutFalloff, hardBoundaryOnly, cfg.city));
        ASSERT_TRUE(lw::MapGenerator::generate(42, withFalloff, gradualCoast, cfg.city));
        ASSERT_EQ(hardBoundaryOnly.terrain.size(), gradualCoast.terrain.size());

        const lw::TilingGeom geometry{type, gradualCoast.cols, gradualCoast.rows};
        int cellsInFalloffBand = 0;
        int interiorCellsTurnedToSea = 0;
        for (int index = 0; index < geometry.cellCount(); ++index) {
            bool touchesHardBoundary = false;
            for (int k = 0; k < geometry.pointNeighborCount(index); ++k)
                if (geometry.pointNeighbor(index, k) < 0) {
                    touchesHardBoundary = true;
                    break;
                }
            const double distance = geometry.cellBoundaryDistance(index);
            if (touchesHardBoundary || distance <= 1e-6 || distance >= 3.0) continue;
            ++cellsInFalloffBand;
            if (hardBoundaryOnly.terrain[static_cast<std::size_t>(index)] != lw::MapTerrain::Sea &&
                gradualCoast.terrain[static_cast<std::size_t>(index)] == lw::MapTerrain::Sea)
                ++interiorCellsTurnedToSea;
        }
        EXPECT_GT(cellsInFalloffBand, 0) << lw::tilingName(type);
        EXPECT_GT(interiorCellsTurnedToSea, 0) << lw::tilingName(type);
    }
}

TEST(MapGen, SkewedPeriodsSampleIndividualBaseCells) {
    const lw::Config cfg = lwtest::loadCfg();
    for (lw::TilingType type : {lw::TilingType::Arch33336, lw::TilingType::Laves33336}) {
        lw::MapGenParams params;
        params.width = 120;
        params.height = 120;
        params.seaRatio = 0.35;
        params.mountainDensity = 0.0;
        params.cityDensity = 0.0;
        params.forceCoast = false;
        params.tiling = type;

        lw::MapDefinition definition;
        ASSERT_TRUE(lw::MapGenerator::generate(9173, params, definition, cfg.city));
        const lw::TilingGeom geometry{type, definition.cols, definition.rows};
        int variedBlocks = 0;
        for (int row = 0; row < geometry.rows; ++row) {
            for (int col = 0; col < geometry.cols; ++col) {
                const auto first = definition.terrain[static_cast<std::size_t>(
                    geometry.cellIndexAt(row, col, 0))];
                for (int base = 1; base < geometry.baseCount(); ++base) {
                    const int index = geometry.cellIndexAt(row, col, base);
                    if (definition.terrain[static_cast<std::size_t>(index)] != first) {
                        ++variedBlocks;
                        break;
                    }
                }
            }
        }
        EXPECT_GT(variedBlocks, 0) << lw::tilingName(type);
    }
}

// ---- 河流系统 §9（R6）：随机成河 ----

// 河边不变量（R5 严格口径）：界内、非地图边界、两侧都是陆地、规范升序无重复。
void expectRiverInvariants(const lw::MapDefinition& definition) {
    const lw::TilingGeom geometry{definition.tiling, definition.cols, definition.rows};
    std::vector<lw::MapEdgeRef> seen;
    for (const lw::MapEdgeRef& ref : definition.rivers) {
        ASSERT_GE(ref.cell, 0);
        ASSERT_LT(ref.cell, geometry.cellCount());
        ASSERT_GE(ref.edge, 0);
        ASSERT_LT(ref.edge, geometry.neighborCount(ref.cell));
        const std::uint64_t key = geometry.edgeKey(ref.cell, ref.edge);
        ASSERT_NE(key, 0u);
        int cell = -1, edge = -1;
        ASSERT_TRUE(geometry.edgeFromKey(key, cell, edge));
        EXPECT_EQ(cell, ref.cell) << "河引用必须是规范侧";
        EXPECT_EQ(edge, ref.edge);
        int a = -1, b = -1;
        geometry.edgeCells(cell, edge, a, b);
        EXPECT_GE(b, 0) << "河不能是地图边界边";
        if (a >= 0 && b >= 0) {
            EXPECT_NE(definition.terrain[static_cast<std::size_t>(a)], lw::MapTerrain::Sea);
            EXPECT_NE(definition.terrain[static_cast<std::size_t>(b)], lw::MapTerrain::Sea);
        }
        seen.push_back(ref);
    }
    std::sort(seen.begin(), seen.end(), [](const lw::MapEdgeRef& x, const lw::MapEdgeRef& y) {
        return x.cell != y.cell ? x.cell < y.cell : x.edge < y.edge;
    });
    EXPECT_EQ(seen, definition.rivers) << "河必须是升序去重的规范列表";
}

lw::MapGenParams riverParams(lw::TilingType tiling, double density) {
    lw::MapGenParams params{56, 48, 0.35, 0.10, 0.02, density, 0.3, false, tiling};
    return params;
}

TEST(MapGen, RiverDensityZeroProducesNoRiversAndKeepsTerrainAndCities) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.0);
    lw::MapDefinition withoutRivers;
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, withoutRivers, cfg.city, cfg.river.gen));
    EXPECT_TRUE(withoutRivers.rivers.empty());

    // 密度 0 → 河生成器在任何 RNG 消耗前返回：改 river.gen 参数不可能影响地形/城市。
    lw::Config::River::Gen extreme = cfg.river.gen;
    extreme.temperature = 1e-3;
    extreme.gradientWeight = 100.0;
    extreme.maxStepsPerRiver = 1;
    lw::MapDefinition sameTerrain;
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, sameTerrain, cfg.city, extreme));
    EXPECT_EQ(sameTerrain.toJson(), withoutRivers.toJson());

    // 有河（0.02）时**海陆分类逐格一致**：海拔分位切海陆发生在河之前、与 RNG 无关，
    // 河只加边。山地无法这样逐格比对——MapDefinition 把"城 + 山"编码成 City（山标记丢失），
    // 而城市分布会因河骰消耗 RNG 而变化；城市本身的差异属预期（§9.9）。
    const auto seaLandSignature = [&](const lw::MapDefinition& source) {
        lw::Map map;
        map.configureCanonical(source.tiling, source.cols, source.rows);
        map.setCityConfig(cfg.city);
        std::string error;
        EXPECT_TRUE(map.loadFromDefinition(source, &error)) << error;
        std::string signature;
        signature.reserve(static_cast<std::size_t>(map.cellCount()));
        for (int i = 0; i < map.cellCount(); ++i)
            signature.push_back(map.atIndex(i).land ? 'L' : 'S');
        return signature;
    };
    const lw::MapGenParams withRivers = riverParams(lw::TilingType::Square, 0.02);
    lw::MapDefinition definition;
    ASSERT_TRUE(lw::MapGenerator::generate(42, withRivers, definition, cfg.city, cfg.river.gen));
    EXPECT_FALSE(definition.rivers.empty());
    EXPECT_EQ(seaLandSignature(definition), seaLandSignature(withoutRivers));
    expectRiverInvariants(definition);
}

TEST(MapGen, RiversAreDeterministicAndSeedDependent) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.03);
    lw::MapDefinition a, b, c;
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, a, cfg.city, cfg.river.gen));
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, b, cfg.city, cfg.river.gen));
    ASSERT_TRUE(lw::MapGenerator::generate(43, params, c, cfg.city, cfg.river.gen));
    EXPECT_FALSE(a.rivers.empty());
    EXPECT_EQ(a.rivers, b.rivers) << "同 (seed, params) 必须同河";
    EXPECT_EQ(a.toJson(), b.toJson());
    EXPECT_NE(a.rivers, c.rivers) << "换 seed 应改变河（概率上必然）";
}

TEST(MapGen, RiversLegalForAllTilingsIncludingSkewed) {
    const lw::Config cfg = lwtest::loadCfg();
    const std::vector<lw::TilingType> tilings = {
        lw::TilingType::Square,     lw::TilingType::Hex,        lw::TilingType::Tri,
        lw::TilingType::Arch33336,  lw::TilingType::Arch33434,  lw::TilingType::Arch3464,
        lw::TilingType::Arch3636,   lw::TilingType::Arch31212,  lw::TilingType::Arch4612,
        lw::TilingType::Arch488,    lw::TilingType::Laves3636,  lw::TilingType::Laves31212,
        lw::TilingType::Laves4612,  lw::TilingType::Laves488,   lw::TilingType::Laves33434,
        lw::TilingType::Laves33336, lw::TilingType::Laves3464};
    int tilingsWithRivers = 0;
    for (const lw::TilingType tiling : tilings) {
        const lw::MapGenParams params = riverParams(tiling, 0.02);
        lw::MapDefinition first;
        ASSERT_TRUE(lw::MapGenerator::generate(2024, params, first, cfg.city, cfg.river.gen))
            << lw::tilingName(tiling);
        expectRiverInvariants(first);
        if (!first.rivers.empty()) ++tilingsWithRivers;
        // 同种子可复现（斜周期走 gridVertex 帧，防止帧混用引入不确定）。
        lw::MapDefinition second;
        ASSERT_TRUE(lw::MapGenerator::generate(2024, params, second, cfg.city, cfg.river.gen))
            << lw::tilingName(tiling);
        EXPECT_EQ(first.rivers, second.rivers) << lw::tilingName(tiling);
        EXPECT_EQ(first.toJson(), second.toJson()) << lw::tilingName(tiling);
    }
    EXPECT_GT(tilingsWithRivers, 10) << "绝大多数密铺应能生成河";
}

// 终止分支的场景覆盖（§9.4）：① 步数上限 ② 内陆河（全陆地）③ 入海（高 mouthWeight）
// ④ 支流交汇/去重（小地图高密度）。
TEST(MapGen, RiverTerminationScenarios) {
    const lw::Config cfg = lwtest::loadCfg();
    // ① 步数上限：每河边数 <= maxStepsPerRiver。
    {
        lw::Config::River::Gen gen = cfg.river.gen;
        gen.maxStepsPerRiver = 3;
        const lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.05);
        lw::MapDefinition definition;
        ASSERT_TRUE(lw::MapGenerator::generate(11, params, definition, cfg.city, gen));
        EXPECT_FALSE(definition.rivers.empty());
        const lw::TilingGeom geometry{definition.tiling, definition.cols, definition.rows};
        // 单河最多 3 条边；河数 = round(density × cellCount) → 总边数不超过 3 倍河数。
        const std::size_t riverCount = static_cast<std::size_t>(
            std::llround(params.riverDensity * static_cast<double>(geometry.cellCount())));
        EXPECT_LE(definition.rivers.size(), 3 * riverCount);
        expectRiverInvariants(definition);
    }
    // ② 内陆河：全陆地（无海）→ 河只能在内陆终止；不变量必须成立。
    {
        lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.05);
        params.seaRatio = 0.0;  // 全陆地
        lw::MapDefinition definition;
        ASSERT_TRUE(lw::MapGenerator::generate(12, params, definition, cfg.city, cfg.river.gen));
        for (lw::MapTerrain terrain : definition.terrain)
            ASSERT_NE(terrain, lw::MapTerrain::Sea);
        EXPECT_FALSE(definition.rivers.empty());
        expectRiverInvariants(definition);
    }
    // ③ 入海：海占比高 + mouthWeight 很大 → 至少一条河终止在临海顶点。
    {
        lw::Config::River::Gen gen = cfg.river.gen;
        gen.mouthWeight = 8.0;
        gen.gradientWeight = 0.2;
        const lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.08);
        lw::MapDefinition definition;
        ASSERT_TRUE(lw::MapGenerator::generate(13, params, definition, cfg.city, gen));
        const lw::TilingGeom geometry{definition.tiling, definition.cols, definition.rows};
        ASSERT_FALSE(definition.rivers.empty());
        // 至少一条河的某个端点临海。
        bool reachedSea = false;
        for (const lw::MapEdgeRef& ref : definition.rivers) {
            int vA = -1, vB = -1;
            ASSERT_TRUE(geometry.cellEdgeVertices(ref.cell, ref.edge, vA, vB));
            for (const int v : {vA, vB}) {
                bool coastal = false;
                for (int k = 0; k < geometry.vertexNeighborCount(ref.cell, v) && !coastal; ++k) {
                    int edgeCell = -1, edgeK = -1;
                    if (geometry.vertexNeighborEdge(ref.cell, v, k, edgeCell, edgeK) < 0)
                        continue;
                    int a = -1, b = -1;
                    geometry.edgeCells(edgeCell, edgeK, a, b);
                    if ((a >= 0 && definition.terrain[static_cast<std::size_t>(a)]
                                      == lw::MapTerrain::Sea) ||
                        (b >= 0 && definition.terrain[static_cast<std::size_t>(b)]
                                      == lw::MapTerrain::Sea))
                        coastal = true;
                }
                if (coastal) reachedSea = true;
            }
            if (reachedSea) break;
        }
        EXPECT_TRUE(reachedSea) << "高 mouthWeight 下应有河走到临海顶点";
        expectRiverInvariants(definition);
    }
    // ④ 支流交汇 + 去重：小地图 + 高密度 → 多条河互相经过，输出仍无重复边。
    {
        lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.5);
        params.width = 32;
        params.height = 32;
        lw::MapDefinition definition;
        ASSERT_TRUE(lw::MapGenerator::generate(14, params, definition, cfg.city, cfg.river.gen));
        EXPECT_FALSE(definition.rivers.empty());
        expectRiverInvariants(definition);  // 含"升序去重"断言
    }
}

// ---- 地形"碎格"不变量（2026-09-24 修 arch/laves 密铺的 1 格碎格）----
// 规则（最终 terrain，按**边邻**判定 = 游戏里真正能走通的连通性）：
//   · 不允许 1 格孤岛（陆地格的边邻全是海）
//   · 不允许 1 格水塘（海格的边邻全是陆）
//   · 不允许孤立山（山格的边邻没有山；山脉/山脊只要连成 ≥2 格即保留）
// 修法只是"消 1 格碎格"的确定性后处理；海拔场/分位阈值/梯度/选山与历史逐位一致。
TEST(MapGen, TerrainHasNoSingleCellSpecksAcrossTilings) {
    const lw::Config cfg = lwtest::loadCfg();
    const std::vector<lw::TilingType> tilings = {
        lw::TilingType::Square,     lw::TilingType::Hex,        lw::TilingType::Tri,
        lw::TilingType::Arch33336,  lw::TilingType::Arch33434,  lw::TilingType::Arch3464,
        lw::TilingType::Arch3636,   lw::TilingType::Arch31212,  lw::TilingType::Arch4612,
        lw::TilingType::Arch488,    lw::TilingType::Laves3636,  lw::TilingType::Laves31212,
        lw::TilingType::Laves4612,  lw::TilingType::Laves488,   lw::TilingType::Laves33434,
        lw::TilingType::Laves33336, lw::TilingType::Laves3464};
    for (const lw::TilingType tiling : tilings) {
        for (const double sea : {0.30, 0.45}) {
            for (const bool forceCoast : {false, true}) {
                for (const std::uint32_t seed : {1u, 42u}) {
                    lw::MapGenParams params{64, 64, sea, 0.12, 0.02, 0.0, 0.3, forceCoast, tiling};
                    lw::MapDefinition definition;
                    ASSERT_TRUE(lw::MapGenerator::generate(seed, params, definition, cfg.city,
                                                           cfg.river.gen))
                        << lw::tilingName(tiling);
                    const lw::TilingGeom geometry{definition.tiling, definition.cols,
                                                  definition.rows};
                    const auto isSea = [&](int index) {
                        return definition.terrain[static_cast<std::size_t>(index)]
                               == lw::MapTerrain::Sea;
                    };
                    const auto isMountain = [&](int index) {
                        return definition.terrain[static_cast<std::size_t>(index)]
                               == lw::MapTerrain::Mountain;
                    };
                    const auto sameCount = [&](int index, const auto& predicate) {
                        int count = 0;
                        for (int k = 0; k < geometry.neighborCount(index); ++k) {
                            const int nb = geometry.neighbor(index, k);
                            if (nb >= 0 && predicate(nb)) ++count;
                        }
                        return count;
                    };
                    for (int index = 0; index < geometry.cellCount(); ++index) {
                        const std::string where = std::string(lw::tilingName(tiling))
                                                  + " sea=" + std::to_string(sea)
                                                  + " coast=" + (forceCoast ? "1" : "0")
                                                  + " seed=" + std::to_string(seed);
                        if (isSea(index)) {
                            EXPECT_GT(sameCount(index, isSea), 0) << "1 格水塘 @" << where;
                        } else {
                            EXPECT_GT(sameCount(index, [&](int i) { return !isSea(i); }), 0)
                                << "1 格孤岛 @" << where;
                            if (isMountain(index)) {
                                EXPECT_GT(sameCount(index, isMountain), 0)
                                    << "孤立山 @" << where;
                            }
                        }
                    }
                }
            }
        }
    }
}

// "陆地占比"滑条语义 = **格数**分位（不是面积），碎格清理只允许动个位数的格。
// 这条断言把语义钉死：将来若有人把它改成面积加权（或清理过度），arch 密铺上会大幅偏离。
TEST(MapGen, SeaRatioStaysCellCountFraction) {
    const lw::Config cfg = lwtest::loadCfg();
    const std::vector<lw::TilingType> tilings = {lw::TilingType::Square, lw::TilingType::Hex,
                                                 lw::TilingType::Arch31212,
                                                 lw::TilingType::Laves4612};
    for (const lw::TilingType tiling : tilings) {
        for (const double sea : {0.30, 0.45, 0.60}) {
            lw::MapGenParams params{64, 64, sea, 0.10, 0.02, 0.0, 0.3, /*forceCoast=*/false, tiling};
            lw::MapDefinition definition;
            ASSERT_TRUE(lw::MapGenerator::generate(42, params, definition, cfg.city, cfg.river.gen))
                << lw::tilingName(tiling);
            const int cells = static_cast<int>(definition.terrain.size());
            int land = 0;
            for (const lw::MapTerrain terrain : definition.terrain) {
                if (terrain != lw::MapTerrain::Sea) ++land;
            }
            const int expected = cells - static_cast<int>(static_cast<double>(cells) * sea);
            // 允许"消 1 格碎格"带来的个位数偏差（< 0.5% 格数），但绝不允许语义级偏离。
            EXPECT_LT(std::abs(land - expected), cells / 200)
                << lw::tilingName(tiling) << " sea=" << sea << " land=" << land
                << " expected=" << expected;
        }
    }
}

}  // namespace
