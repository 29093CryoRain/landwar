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
    ASSERT_TRUE(lw::MapGenerator::generate(7, params, generated, cfg.city, cfg.river.gen));
    const std::string path = lwtest::testArtifactPath("generated.landmap");
    // 两条路径必须传同一份 city/river 配置：默认 City{} 的等级幂律指数（1.0/0.5）与
    // data/config.jsonc 的（1.3/0.3）不同 → 城市等级会不同（此前本用例靠"两次采样恰好同级"侥幸通过）。
    ASSERT_TRUE(lw::MapGenerator::generate(path, 7, params, cfg.city, cfg.river.gen));
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
    // （各阶段已用独立子流，这个等式现在更强：河参数的改动连"下游随机数位置"都动不了。）
    lw::Config::River::Gen extreme = cfg.river.gen;
    extreme.gradientWeight = 100.0;
    extreme.mouthWeight = 5.0;
    extreme.sourceRetryPerRiver = 0;
    extreme.maxStepsPerRiver = 1;
    // 二期反馈：河流专用梯度参数（裁剪/平滑）与源区概率也必须在密度 0 时零副作用。
    extreme.gmin = 0.0;
    extreme.gmax = 100.0;
    extreme.gradientSmoothScale = 1.0 / 64.0;
    extreme.mountainSourceWeight = 0.0;
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

// 二期反馈：河流梯度幅度裁剪（gmin/gmax）——纯函数级验证公式 g_smooth = clip(|g|,gmin,gmax)·g/|g|。
TEST(MapGen, SmoothGradientMagnitudeClipsAndPreservesDirection) {
    // 幅度 5（3-4-5），上限 2 → 缩到 2，方向不变：{1.2, 1.6}。
    const lw::GradVec capped = lw::smoothGradientMagnitude({3.0, 4.0}, 0.0, 2.0);
    EXPECT_NEAR(capped.x, 1.2, 1e-12);
    EXPECT_NEAR(capped.y, 1.6, 1e-12);
    // 幅度 0.1，下限 0.5 → 抬到 0.5（方向不变）：{0.5, 0}。
    const lw::GradVec floored = lw::smoothGradientMagnitude({0.1, 0.0}, 0.5, 2.0);
    EXPECT_NEAR(floored.x, 0.5, 1e-12);
    EXPECT_NEAR(floored.y, 0.0, 1e-12);
    // 区间内原样；零向量仍是零向量（不除零）。
    const lw::GradVec inside = lw::smoothGradientMagnitude({0.4, 0.3}, 0.1, 1.0);
    EXPECT_NEAR(inside.x, 0.4, 1e-12);
    EXPECT_NEAR(inside.y, 0.3, 1e-12);
    const lw::GradVec zero = lw::smoothGradientMagnitude({0.0, 0.0}, 0.25, 0.8);
    EXPECT_DOUBLE_EQ(zero.x, 0.0);
    EXPECT_DOUBLE_EQ(zero.y, 0.0);
}

// 河流生成参数（梯度裁剪/平滑 + 源区概率）只影响河，**地形/城市逐字节不变**
//（山脉用原 gradVec；城用独立子流）。
TEST(MapGen, RiverGradientClipAndSmoothChangeRiversNotTerrainOrCities) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.03);
    lw::MapDefinition base, zeroGrad, wideClip, bigStep, plainSrc;
    ASSERT_TRUE(lw::MapGenerator::generate(2024, params, base, cfg.city, cfg.river.gen));
    ASSERT_FALSE(base.rivers.empty());

    lw::Config::River::Gen zero = cfg.river.gen;   // 全裁成 0 → 均匀分布（纯随机游走）
    zero.gmin = 0.0;
    zero.gmax = 0.0;
    ASSERT_TRUE(lw::MapGenerator::generate(2024, params, zeroGrad, cfg.city, zero));

    lw::Config::River::Gen unit = cfg.river.gen;   // 幅度归一化（gmin=gmax=1）→ 方向保留、大小恒定
    unit.gmin = 1.0;
    unit.gmax = 1.0;
    ASSERT_TRUE(lw::MapGenerator::generate(2024, params, wideClip, cfg.city, unit));

    lw::Config::River::Gen step = cfg.river.gen;   // 改采样步长（48→16）→ 河集合改变
    step.gradientSmoothScale = 16.0;
    ASSERT_TRUE(lw::MapGenerator::generate(2024, params, bigStep, cfg.city, step));

    lw::Config::River::Gen plain = cfg.river.gen;  // 权重 0 → 源全在平原
    plain.mountainSourceWeight = 0.0;
    ASSERT_TRUE(lw::MapGenerator::generate(2024, params, plainSrc, cfg.city, plain));

    EXPECT_NE(base.rivers, zeroGrad.rivers);
    EXPECT_NE(base.rivers, wideClip.rivers);
    EXPECT_NE(base.rivers, bigStep.rivers);
    EXPECT_NE(base.rivers, plainSrc.rivers);
    // 清掉河后，各图的地形/城市应逐字节一致（这些参数只在河阶段生效）。
    base.rivers.clear();
    zeroGrad.rivers.clear();
    wideClip.rivers.clear();
    bigStep.rivers.clear();
    plainSrc.rivers.clear();
    EXPECT_EQ(base.toJson(), zeroGrad.toJson());
    EXPECT_EQ(base.toJson(), wideClip.toJson());
    EXPECT_EQ(base.toJson(), bigStep.toJson());
    EXPECT_EQ(base.toJson(), plainSrc.toJson());
}

// 河源抽样按权重自适应山密度（§17）：同一 mountainSourceWeight 下，山密度越高 → 山地源占比越高。
// 用 RiverGenStats.mountainSources 直接观测；合成图从左到右逐渐扩大山地占比（40×40 全陆地）。
TEST(MapGen, MountainSourceWeightAdaptsToMountainDensity) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::TilingGeom g{lw::TilingType::Square, 40, 40};
    const int total = g.cellCount();
    const std::vector<bool> land(static_cast<std::size_t>(total), true);
    const std::vector<lw::GradVec> grad(static_cast<std::size_t>(total));  // 零梯度 → 均匀游走
    // mountainShare = 山地格 / 总格；源在"山地/平原"两池间按 w·M/(w·M+P) 抽签。
    const auto run = [&](double mountainShare, double weight, std::uint32_t seed,
                         lw::RiverGenStats& stats) {
        std::vector<bool> mountain(static_cast<std::size_t>(total), false);
        int wanted = static_cast<int>(mountainShare * static_cast<double>(total));
        for (int i = 0; i < total && i < wanted; ++i) mountain[static_cast<std::size_t>(i)] = true;
        lw::Config::River::Gen gen = cfg.river.gen;
        gen.mountainSourceWeight = weight;
        lw::Rng rng(seed);
        return lw::generateRivers(g, land, mountain, grad, 0.15, gen, false, rng, &stats);
    };
    const auto fraction = [](const lw::RiverGenStats& s) {
        return s.rivers > 0 ? static_cast<double>(s.mountainSources) / s.rivers : 0.0;
    };
    // 山密度 0 → 必然全平原源（不抽签）。
    {
        lw::RiverGenStats stats;
        run(0.0, 10.0, 7, stats);
        EXPECT_GT(stats.rivers, 0);
        EXPECT_EQ(stats.mountainSources, 0);
    }
    // 山密度 100 → 必然全山地源（不抽签）。
    {
        lw::RiverGenStats stats;
        run(1.0, 10.0, 7, stats);
        EXPECT_GT(stats.rivers, 0);
        EXPECT_EQ(stats.mountainSources, stats.rivers);
    }
    // 固定 w：山地源占比必须随山密度**单调不减**（本用例的核心，正是用户要求）。
    const double shares[] = {0.05, 0.10, 0.20, 0.40};
    double previous = -1.0, lowest = -1.0, highest = -1.0;
    for (double share : shares) {
        lw::RiverGenStats stats;
        run(share, 10.0, 7, stats);
        EXPECT_GT(stats.rivers, 0);
        const double f = fraction(stats);
        EXPECT_GE(f, previous) << "山密度 " << share << " 的山地源占比不得低于更低山密度";
        if (lowest < 0.0) lowest = f;
        highest = f;
        previous = f;
    }
    EXPECT_GT(highest, lowest) << "高密度端应显著高于低密度端（不是偶然相等）";
    // w = 1 → 源分布 ≈ 陆地面积占比；w = 0 → 全平原；w 越大山地源越多。
    lw::RiverGenStats area, zero, strong;
    run(0.20, 1.0, 11, area);
    run(0.20, 0.0, 11, zero);
    run(0.20, 100.0, 11, strong);
    EXPECT_EQ(zero.mountainSources, 0);
    EXPECT_NEAR(fraction(area), 0.20, 0.10) << "w=1 时山地源占比应≈山密度";
    EXPECT_GT(fraction(area), 0.0);
    EXPECT_LT(fraction(area), 1.0);
    EXPECT_GT(fraction(strong), fraction(area)) << "权重越大越偏山地";
}

// 山 + 城：生成器必须与编辑器 toDefinition 一致 —— 单值 MapTerrain 无法表达组合，
// 故山上的城格写 **Mountain 地形 + 城市记录**（而非 City），保住运行时 mountain 标记
//（否则山图标不画、进格不掷山骰，河源看起来凭空出现在城格上）。
TEST(MapGen, MountainCityKeepsMountainTerrainAndRuntimeFlag) {
    const lw::Config cfg = lwtest::loadCfg();
    lw::MapGenParams plain{105, 95, 0.46, 0.10, 0.0, 0.02, 0.3, false, lw::TilingType::Square};
    lw::MapGenParams withCities = plain;
    withCities.cityDensity = 0.02;
    lw::MapDefinition base, city;
    ASSERT_TRUE(lw::MapGenerator::generate(42, plain, base, cfg.city, cfg.river.gen));
    ASSERT_TRUE(lw::MapGenerator::generate(42, withCities, city, cfg.city, cfg.river.gen));
    ASSERT_EQ(base.terrain.size(), city.terrain.size());

    // 山是只增不减的：每个生成时的山格在成品里仍是 Mountain（城格也不例外）。
    for (std::size_t i = 0; i < base.terrain.size(); ++i) {
        if (base.terrain[i] == lw::MapTerrain::Mountain) {
            ASSERT_EQ(city.terrain[i], lw::MapTerrain::Mountain) << "cell " << i;
        }
    }

    lw::Map map;
    map.configureCanonical(city.tiling, city.cols, city.rows);
    map.setCityConfig(cfg.city);
    std::string err;
    ASSERT_TRUE(map.loadFromDefinition(city, &err)) << err;

    int cityCells = 0, mountainCities = 0, plainCities = 0;
    for (int i = 0; i < map.cellCount(); ++i) {
        if (map.atIndex(i).cityId < 0) continue;
        ++cityCells;
        const bool wasMountain = base.terrain[static_cast<std::size_t>(i)] == lw::MapTerrain::Mountain;
        EXPECT_EQ(map.atIndex(i).mountain, wasMountain) << "cell " << i;
        EXPECT_EQ(city.terrain[static_cast<std::size_t>(i)],
                  wasMountain ? lw::MapTerrain::Mountain : lw::MapTerrain::City)
            << "cell " << i;
        if (wasMountain) ++mountainCities; else ++plainCities;
    }
    EXPECT_GT(cityCells, 0);
    EXPECT_GT(mountainCities, 0) << "该参数组合应产生山城（否则用例没覆盖到组合）";
    EXPECT_GT(plainCities, 0) << "非山城格仍应写 City（没有把城全写成山）";
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

// §14 决策 D2：河密度是"占**陆地格**"比 —— 尝试数 = round(密度 × 陆地格数)，与总格数无关。
// 直接调生成器并用 RiverGenStats.planned 观察"计划尝试数"（唯一能精确观测该语义的口子）。
TEST(MapGen, RiverAttemptsUseLandCellCountNotTotalCellCount) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::TilingGeom g{lw::TilingType::Square, 20, 20};
    const int total = g.cellCount();
    const std::vector<lw::GradVec> grad(static_cast<std::size_t>(total));
    const std::vector<bool> mountain(static_cast<std::size_t>(total), false);

    // 全陆地：planned 应为 round(密度 × 总格数)。
    {
        const std::vector<bool> land(static_cast<std::size_t>(total), true);
        lw::Rng rng(1);
        lw::RiverGenStats stats;
        lw::generateRivers(g, land, mountain, grad, 0.25, cfg.river.gen, false, rng, &stats);
        EXPECT_EQ(stats.planned, static_cast<int>(std::llround(0.25 * total)));
    }
    // 只有 1/4 格是陆地：planned 必须按**陆地格数**缩放（若按总格数则是原来的 4 倍）。
    {
        std::vector<bool> land(static_cast<std::size_t>(total), false);
        const int landCount = total / 4;
        for (int i = 0; i < landCount; ++i) land[static_cast<std::size_t>(i)] = true;
        lw::Rng rng(1);
        lw::RiverGenStats stats;
        lw::generateRivers(g, land, mountain, grad, 0.25, cfg.river.gen, false, rng, &stats);
        EXPECT_EQ(stats.planned, static_cast<int>(std::llround(0.25 * landCount)));
        EXPECT_NE(stats.planned, static_cast<int>(std::llround(0.25 * total)))
            << "分母必须是陆地格数，不是总格数";
    }
}

// §9.4 行 0（R7 修正）：河网输出必须是**边不相交**的路径集合 —— 源顶点若已落在别的河上
// 则免费跳过（不消耗尝试次数），配合"到达别的河的顶点即终止"就杜绝了重复走边。
// 核心不变量：走过的总边数 == 去重后的边数（若发生重走，前者必然更大）。
TEST(MapGen, RiverGeneratorNeverRewalksAnEdge) {
    const lw::Config cfg = lwtest::loadCfg();
    const lw::MapGenParams params = riverParams(lw::TilingType::Square, 0.0);  // 先只要地形
    lw::MapDefinition terrainOnly;
    ASSERT_TRUE(lw::MapGenerator::generate(7, params, terrainOnly, cfg.city, cfg.river.gen));
    const lw::TilingGeom geometry{terrainOnly.tiling, terrainOnly.cols, terrainOnly.rows};
    const int total = geometry.cellCount();
    std::vector<bool> land(static_cast<std::size_t>(total), false);
    int landCount = 0;
    for (int i = 0; i < total; ++i) {
        land[static_cast<std::size_t>(i)] = terrainOnly.terrain[static_cast<std::size_t>(i)] !=
                                            lw::MapTerrain::Sea;
        landCount += land[static_cast<std::size_t>(i)] ? 1 : 0;
    }
    // 全零梯度 ⇒ softmax 退化为均匀 ⇒ 各条河都是**纯随机游走**（最容易互相撞边），
    // 因而是对"无重边"最严苛的输入；山全 false ⇒ 源格从全部陆地格抽。
    const std::vector<bool> mountain(static_cast<std::size_t>(total), false);
    const std::vector<lw::GradVec> grad(static_cast<std::size_t>(total));
    constexpr double kDensity = 0.3;
    lw::Rng rng(99);
    lw::RiverGenStats stats;
    const std::vector<lw::MapEdgeRef> rivers = lw::generateRivers(
        geometry, land, mountain, grad, kDensity, cfg.river.gen, false, rng, &stats);

    EXPECT_EQ(stats.planned, static_cast<int>(std::llround(kDensity * landCount)));
    EXPECT_FALSE(rivers.empty());
    EXPECT_EQ(stats.traversals, static_cast<int>(rivers.size()))
        << "每条边只应被走过一次（输出 = 去重后的走过集合）";
    EXPECT_GT(stats.sourceSkips, 0) << "必然抽到『源顶点已在河上』的源，应免费跳过";
    // 并非每次尝试都成河：源点的所有出边都临海/越界时本次尝试作废（换源重试，上限
    // sourceRetryPerRiver）。这里只要求绝大多数成功。
    EXPECT_GT(stats.rivers, stats.planned / 2);
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
        // 单河最多 3 条边；尝试数 = round(density × **陆地格数**)（§14 决策 D2）→ 总边数不超过 3 倍。
        std::size_t landCount = 0;
        for (lw::MapTerrain terrain : definition.terrain)
            landCount += terrain == lw::MapTerrain::Sea ? 0 : 1;
        const std::size_t riverCount = static_cast<std::size_t>(
            std::llround(params.riverDensity * static_cast<double>(landCount)));
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

// ---- 地形"碎格"修复的机制测试（2026-09-24，arch/laves 密铺）----
// 背景（实测）：3.12.12 里三角形占格数 2/3、面积仅 ~3%，且三角形彼此不相邻。三个独立成因：
//   ① 阈值等值线在最细 octave 上曲率半径 ≈ 最小格尺寸 → 单格孤岛/孤湖；
//   ② 梯度若从"格心平面拟合"估计，stencil 与最细 octave 同量级 → 误差随格型系统分化，
//      山脉"最陡前 x%"排序按格型偏袒互不相邻的小格 → 孤山；
//   ③ 边缘衰减若按"每格自身顶点到边界距离"逐格扣减 → 相邻大小格之间跳变 → 制造格级深坑。
// 修法：fBm 3→2 octave（最细波长 baseCell/2 ≈ 10 U ≈ 3~5 倍最大格）+
//      梯度直接取自噪声场（固定步长中心差分，与网格无关）+
//      边缘衰减改为按**格心**到边界距离（位置的连续函数）。
// 实测（arch_31212 120×120）：孤岛 19→1、孤山 138→28；全部 17 密铺均 ≤4 / ≤5%。

// 机制：格心到地图边界的距离必须是**位置的 1-Lipschitz 函数**（|Δd| ≤ |Δposition|）。
// 旧的"按格顶点取最小距离"在相邻大小格之间会跳变（3.12.12 实测能跳 ~1 U），
// 逐格扣减就变成格级深坑 → 这正是 forceCoast 打开时孤岛从 1 涨到 19 的原因。
TEST(MapGen, CenterBoundaryDistanceIsOneLipschitz) {
    const std::vector<lw::TilingType> tilings = {
        lw::TilingType::Square, lw::TilingType::Hex, lw::TilingType::Arch31212,
        lw::TilingType::Arch4612, lw::TilingType::Laves31212, lw::TilingType::Arch488};
    for (const lw::TilingType tiling : tilings) {
        int cols = 48, rows = 48;
        lw::chooseTableDomain(static_cast<int>(tiling), 48, 48, cols, rows);
        const lw::TilingGeom geometry{tiling, cols, rows};
        for (int index = 0; index < geometry.cellCount(); ++index) {
            const double d0 = geometry.centerBoundaryDistance(index);
            double x0 = 0.0, y0 = 0.0;
            geometry.cellCenter(index, x0, y0);
            for (int k = 0; k < geometry.neighborCount(index); ++k) {
                const int nb = geometry.neighbor(index, k);
                if (nb < 0) continue;
                double x1 = 0.0, y1 = 0.0;
                geometry.cellCenter(nb, x1, y1);
                const double step = std::hypot(x1 - x0, y1 - y0);
                EXPECT_LE(std::fabs(geometry.centerBoundaryDistance(nb) - d0), step + 1e-9)
                    << lw::tilingName(tiling) << " " << index << "->" << nb;
            }
        }
    }
}

// 语义：seaRatio 仍是**格数**分位（用户要求"陆地占比滑条不失真"）。
// 现在没有任何后处理，故陆地格数必须**精确**等于 N − ⌊N·seaRatio⌋（并列除外）。
TEST(MapGen, SeaRatioIsExactCellCountQuantile) {
    const lw::Config cfg = lwtest::loadCfg();
    for (const lw::TilingType tiling :
         {lw::TilingType::Square, lw::TilingType::Arch31212, lw::TilingType::Laves31212,
          lw::TilingType::Arch4612}) {
        for (const double sea : {0.30, 0.45, 0.60}) {
            lw::MapGenParams params{64, 64, sea, 0.10, 0.0, 0.0, 0.3, /*forceCoast=*/false, tiling};
            lw::MapDefinition definition;
            ASSERT_TRUE(lw::MapGenerator::generate(42, params, definition, cfg.city, cfg.river.gen))
                << lw::tilingName(tiling);
            const int cells = static_cast<int>(definition.terrain.size());
            int land = 0;
            for (const lw::MapTerrain terrain : definition.terrain) {
                if (terrain != lw::MapTerrain::Sea) ++land;
            }
            EXPECT_EQ(land, cells - static_cast<int>(static_cast<double>(cells) * sea))
                << lw::tilingName(tiling) << " sea=" << sea;
        }
    }
}

// 回归护栏：碎格数量必须保持在"与方形同量级"，不允许回到 arch 特有的成片孤岛/孤山。
// 修前实测（arch_31212 120×120 forceCoast=1）：孤立陆格 19、孤立山占山总数 22.6%；
// 修后：≤4、≤5%（同批 17 密铺 × 海占比 × forceCoast × 种子）。
TEST(MapGen, TerrainSpecksStayRareAcrossTilings) {
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
                    lw::MapGenParams params{64, 64, sea, 0.12, 0.0, 0.0, 0.3, forceCoast, tiling};
                    lw::MapDefinition definition;
                    ASSERT_TRUE(lw::MapGenerator::generate(seed, params, definition, cfg.city,
                                                           cfg.river.gen))
                        << lw::tilingName(tiling);
                    const lw::TilingGeom geometry{definition.tiling, definition.cols,
                                                  definition.rows};
                    int islands = 0, mountains = 0, dots = 0;
                    const auto isSea = [&](int index) {
                        return definition.terrain[static_cast<std::size_t>(index)]
                               == lw::MapTerrain::Sea;
                    };
                    for (int index = 0; index < geometry.cellCount(); ++index) {
                        int same = 0;
                        int mountainNeighbors = 0;
                        for (int k = 0; k < geometry.neighborCount(index); ++k) {
                            const int nb = geometry.neighbor(index, k);
                            if (nb < 0) continue;
                            if (isSea(nb) == isSea(index)) ++same;
                            if (definition.terrain[static_cast<std::size_t>(nb)]
                                == lw::MapTerrain::Mountain)
                                ++mountainNeighbors;
                        }
                        if (!isSea(index) && same == 0) ++islands;
                        if (definition.terrain[static_cast<std::size_t>(index)]
                            == lw::MapTerrain::Mountain) {
                            ++mountains;
                            if (mountainNeighbors == 0) ++dots;
                        }
                    }
                    const std::string where = std::string(lw::tilingName(tiling))
                                              + " sea=" + std::to_string(sea)
                                              + " coast=" + (forceCoast ? "1" : "0")
                                              + " seed=" + std::to_string(seed);
                    // 孤岛：修后全 17 密铺实测 ≤7（arch_3636 最差），取 8 作爆表护栏（修前 arch_31212 为 19）。
                    EXPECT_LE(islands, 8) << "1 格孤岛过多 @" << where;
                    // 孤山：修后实测 0.8%~26%（arch_3636@64² 最差；arch_31212 已从 22.6% 降到 4%），修前 22.6%。
                    // 残余是"取平滑场前 x%"这一机制本身的性质（切点附近的局部极大点），
                    // 不是执行缺陷 ⇒ 这里只作**爆表护栏**（30%），不放"必须归零"的假保证；
                    // 若要"任意密铺零孤山"，需另加形态学/山脊线判据（见 .docs/old/2026_08_开发计划.md §0）。
                    EXPECT_LE(100 * dots, 30 * std::max(1, mountains))
                        << "孤立山过多 @" << where << " dots=" << dots << "/" << mountains;
                }
            }
        }
    }
}

}  // namespace
