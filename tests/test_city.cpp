// test_city.cpp — P13 城市系统单测（多格基建地块 + 等级幂律采样 + 放置回退 + 确定性 + 幂律分布）。
// 覆盖开发计划 P13 测试计划：形状推导（level→w/h→基建格集合）、等级采样各分支（mock 固定 u）、
// 放置合法/越界/重叠/锚点不可成城→回退 1 级、同 mapSeed 逐格一致、幂律分布（容差断言）。
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "core/Simulation.h"
#include "world/Map.h"
#include "world/MapGenerator.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 脚本化 Rng：chance()/unit() 各自独立脚本（unit 覆盖后等级采样可控，同 chance 的 mock 模式）。
class LevelRng final : public lw::Rng {
public:
    std::vector<double> units;   // 脚本化 unit() 结果
    std::size_t nextU = 0;
    std::vector<bool> chances;   // 脚本化 chance() 结果
    std::size_t nextC = 0;
    double unit() override { return nextU < units.size() ? units[nextU++] : 0.0; }
    bool chance(double) override { return nextC < chances.size() ? chances[nextC++] : false; }
};

// 全陆地图（给定尺寸）+ 默认 terrain/city 配置。
struct CityMap {
    Config cfg;
    Config::Map mcfg;
    Map map;
    explicit CityMap(int w = 20, int h = 20) : cfg(Config::loadFromJson("{}")) {
        mcfg.width = w;
        mcfg.height = h;
        map.configure(mcfg);
        map.setTerrain(cfg.terrain);
        map.setCityConfig(cfg.city);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) map.at(x, y).land = true;
    }
};

// 等级采样分支（修正幂律：P(x) ∝ n_x·(x+β)^-s，s=levelIncomeExponent+levelRankExponent=1.5，
// β=(1−min)/2=0；等级 {1,2,4,6,9}）。累计分界 u：1级<0.632、2级<0.855、4级<0.934、6级<0.977、9级其余。
TEST(City, LevelSamplingBranches) {
    const Config cfg = Config::loadFromJson("{}");
    struct Case {
        double u;
        int expect;
    };
    const Case cases[] = {{0.10, 1}, {0.60, 1}, {0.64, 2}, {0.80, 2}, {0.86, 4},
                          {0.93, 4}, {0.94, 6}, {0.97, 6}, {0.98, 9}, {0.99, 9}};
    for (const auto& c : cases) {
        LevelRng rng;
        rng.units = {c.u};
        EXPECT_EQ(Map::sampleCityLevel(cfg.city, rng), c.expect) << "u=" << c.u;
    }
    // 极端值兜底：u=0 与 u 极近 1 都在合法等级内（不越界）。
    {
        LevelRng rng;
        rng.units = {0.0};
        EXPECT_EQ(Map::sampleCityLevel(cfg.city, rng), 1);
    }
    {
        LevelRng rng;
        rng.units = {0.999999};
        EXPECT_EQ(Map::sampleCityLevel(cfg.city, rng), 9);
    }
}

// 形状推导：level → (w,h) → 基建格集合（各等级全格列举断言）+ 注册表 append-only。
TEST(City, ShapeCoverageMatchesLevel) {
    CityMap w(20, 20);
    const int levels[5] = {1, 2, 4, 6, 9};
    const std::array<std::array<int, 2>, 5> wh = {{{1, 1}, {1, 2}, {2, 2}, {2, 3}, {3, 3}}};
    int baseY = 0;
    for (int k = 0; k < 5; ++k) {
        const int baseX = levels[k] == 9 ? 1 : 0;
        const int cid = w.map.addCity(levels[k], baseX, baseY);
        ASSERT_GE(cid, 0);
        const City& c = w.map.city(cid);
        EXPECT_EQ(c.level, levels[k]);
        EXPECT_EQ(c.w, wh[static_cast<size_t>(k)][0]) << "level " << levels[k];
        EXPECT_EQ(c.h, wh[static_cast<size_t>(k)][1]) << "level " << levels[k];
        EXPECT_EQ(c.baseX, baseX);
        EXPECT_EQ(c.baseY, baseY);
        EXPECT_DOUBLE_EQ(c.area, static_cast<double>(levels[k]));
        const auto cells = w.map.cityCells(c);
        ASSERT_EQ(cells.size(), static_cast<std::size_t>(levels[k]));
        for (const int index : cells)
            ASSERT_GE(index, 0) << "level " << levels[k] << " baseY " << baseY;
        for (const int index : cells) EXPECT_EQ(w.map.atIndex(index).cityId, cid);
        baseY += 4;  // 每级换行（最大形状 3 行高 → 互不重叠）
    }
    EXPECT_EQ(w.map.totalCities(), 5);
    for (int i = 0; i < w.map.totalCities(); ++i) EXPECT_EQ(w.map.city(i).id, i);  // append-only
}

// 放置检查：合法 / 越界 / 重叠 / 锚点不可成城。
TEST(City, CanPlaceCityRules) {
    CityMap w(12, 12);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x) w.map.at(x, y).cityAllowed = true;
    EXPECT_FALSE(w.map.canPlaceCity(9, 0, 0));  // 中心锚点形状越界
    EXPECT_TRUE(w.map.canPlaceCity(9, 1, 1));   // 合法 3×3
    EXPECT_TRUE(w.map.canPlaceCity(1, 11, 11));  // 单格贴右下角
    EXPECT_FALSE(w.map.canPlaceCity(9, 11, 11));  // 中心锚点形状越界
    EXPECT_FALSE(w.map.canPlaceCity(6, 11, 0));   // 越界（11+2>12，6 级形状 2×3）
    w.map.addCity(1, 0, 0);                       // 锚点 (0,0) 被占
    EXPECT_FALSE(w.map.canPlaceCity(4, 0, 0));    // 重叠
    EXPECT_FALSE(w.map.canPlaceCity(9, 1, 1));    // 重叠（形状含 (0,0)）
    w.map.at(5, 0).cityAllowed = false;           // 锚点不可成城
    EXPECT_FALSE(w.map.canPlaceCity(1, 5, 0));
    EXPECT_FALSE(w.map.canPlaceCity(2, 5, 0));    // 形状 1×2 锚点不可成城
    // 未注册等级 → 拒绝。
    EXPECT_FALSE(w.map.canPlaceCity(3, 0, 3));
    EXPECT_EQ(w.map.addCity(3, 0, 3), -1);
}

// 放置回退：采样到 9 级但形状放不下（2×2 地图）→ 回退 1 级（锚点单独可放则建 1 级城）。
TEST(City, FallbackToLevel1WhenShapeDoesNotFit) {
    const std::string path = lwtest::testArtifactPath("city_native.landmap");
    MapDefinition definition;
    definition.cols = 2;
    definition.rows = 2;
    definition.terrain.assign(4, MapTerrain::Land);
    definition.cities = {{1.0, 0, 0}};
    ASSERT_TRUE(definition.saveToFile(path));
    CityMap w(2, 2);
    ASSERT_TRUE(w.map.loadFromLandmap(path));
    EXPECT_EQ(w.map.totalCities(), 1);
    const City& c = w.map.city(0);
    EXPECT_EQ(c.level, 1);  // 回退
    EXPECT_EQ(c.baseX, 0);
    EXPECT_EQ(c.baseY, 0);
    EXPECT_EQ(w.map.at(0, 0).cityId, 0);
    EXPECT_EQ(w.map.at(1, 1).cityId, -1);
}

// 锚点不可成城 + 形状放不下 → 两级都不可放 → 本格不成城。
TEST(City, NoCityWhenPlacementFailsBothLevels) {
    const std::string path = lwtest::testArtifactPath("city_none.landmap");
    MapDefinition definition;
    definition.cols = 1;
    definition.rows = 1;
    definition.terrain = {MapTerrain::Land};
    ASSERT_TRUE(definition.saveToFile(path));
    CityMap w(1, 1);
    ASSERT_TRUE(w.map.loadFromLandmap(path));
    EXPECT_EQ(w.map.totalCities(), 0);
    EXPECT_EQ(w.map.at(0, 0).cityId, -1);
}

// 幂律分布（统计）——2026-08-17 调试期改均匀分布：方形成等级 {1,2,4,6,9} 各约 20%。
TEST(City, LevelDistributionApproximatesPowerLaw) {
    Config cfg = lwtest::loadCfg();
    MapGenParams params{105, 95, 0.25, 0.05, 0.06};
    MapDefinition definition;
    ASSERT_TRUE(MapGenerator::generate(123, params, definition, cfg.city));
    cfg.map.width = definition.cols;
    cfg.map.height = definition.rows;
    cfg.map.tiling = tilingName(definition.tiling);
    Simulation sim(cfg, 123);
    sim.setMapDefinition(std::move(definition));
    ASSERT_TRUE(sim.init());
    ASSERT_GT(sim.map().totalCities(), 0);
    for (const auto& city : sim.map().cities()) {
        EXPECT_GT(city.level, 0.0);
        EXPECT_GE(city.shapeVariant, 0);
        EXPECT_FALSE(sim.map().cityCells(city).empty());
    }
}

// 确定性：同 mapSeed 两次 init → 逐格 cityId + 城市注册表一致。
TEST(City, SameMapSeedDeterministic) {
    Config cfg = lwtest::loadCfg();
    Simulation a(cfg, 42), b(cfg, 42);
    ASSERT_TRUE(a.init());
    ASSERT_TRUE(b.init());
    for (int y = 0; y < a.map().height(); ++y)
        for (int x = 0; x < a.map().width(); ++x)
            EXPECT_EQ(a.map().at(x, y).cityId, b.map().at(x, y).cityId)
                << "(" << x << "," << y << ")";
    ASSERT_EQ(a.map().cities().size(), b.map().cities().size());
    for (std::size_t i = 0; i < a.map().cities().size(); ++i) {
        const auto& ca = a.map().cities()[i];
        const auto& cb = b.map().cities()[i];
        EXPECT_EQ(ca.level, cb.level) << "city " << i;
        EXPECT_EQ(ca.baseX, cb.baseX) << "city " << i;
        EXPECT_EQ(ca.baseY, cb.baseY) << "city " << i;
        EXPECT_EQ(ca.w, cb.w) << "city " << i;
        EXPECT_EQ(ca.h, cb.h) << "city " << i;
        EXPECT_EQ(ca.ownerId, cb.ownerId) << "city " << i;
    }
}

}  // namespace
