#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "core/Config.h"
#include "core/Simulation.h"
#include "world/Map.h"
#include "world/MapGenerator.h"
#include "TestUtil.h"

namespace {

TEST(MapDefinition, NativeRoundTripPreservesTerrainAndCities) {
    lw::MapDefinition original;
    original.cols = 4;
    original.rows = 3;
    original.terrain = {
        lw::MapTerrain::Sea,      lw::MapTerrain::Land, lw::MapTerrain::Mountain,
        lw::MapTerrain::Land,     lw::MapTerrain::City, lw::MapTerrain::Land,
        lw::MapTerrain::Mountain, lw::MapTerrain::Sea,  lw::MapTerrain::Land,
        lw::MapTerrain::Land,     lw::MapTerrain::Land, lw::MapTerrain::City,
    };
    original.cities = {{1.0, 5, 0}, {1.0, 10, 0}};

    const std::string path = lwtest::testArtifactPath("native_roundtrip.landmap");
    ASSERT_TRUE(original.saveToFile(path));
    lw::MapDefinition restored;
    std::string error;
    ASSERT_TRUE(lw::MapDefinition::loadFromFile(path, restored, &error)) << error;
    EXPECT_EQ(restored.cols, original.cols);
    EXPECT_EQ(restored.rows, original.rows);
    EXPECT_EQ(restored.tiling, original.tiling);
    EXPECT_EQ(restored.terrain, original.terrain);
    ASSERT_EQ(restored.cities.size(), original.cities.size());
    for (std::size_t i = 0; i < restored.cities.size(); ++i) {
        EXPECT_DOUBLE_EQ(restored.cities[i].level, original.cities[i].level);
        EXPECT_EQ(restored.cities[i].baseIndex, original.cities[i].baseIndex);
        EXPECT_EQ(restored.cities[i].shapeVariant, original.cities[i].shapeVariant);
    }
    std::remove(path.c_str());
}

TEST(MapDefinition, RejectsCityWhoseDerivedCellsAreNotLand) {
    const std::string path = lwtest::testArtifactPath("native_invalid_city.landmap");
    lw::MapDefinition definition;
    definition.cols = 2;
    definition.rows = 2;
    definition.terrain.assign(4, lw::MapTerrain::Sea);
    definition.terrain[0] = lw::MapTerrain::Land;
    definition.cities = {{1.0, 1, 0}};
    ASSERT_TRUE(definition.saveToFile(path));

    lw::Config cfg = lwtest::loadCfg();
    cfg.map.width = 2;
    cfg.map.height = 2;
    lw::Map map;
    map.configure(cfg.map);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    std::string error;
    EXPECT_FALSE(map.loadFromLandmap(path, &error));
    EXPECT_NE(error.find("occupied"), std::string::npos);
    std::remove(path.c_str());
}

TEST(MapDefinition, LoadInstallsExactTerrainAndCityVariantWithoutRng) {
    lw::Config cfg = lwtest::loadCfg();
    lw::MapDefinition definition;
    definition.cols = 12;
    definition.rows = 12;
    definition.terrain.assign(144, lw::MapTerrain::Land);
    definition.terrain[0] = lw::MapTerrain::Sea;
    definition.terrain[13] = lw::MapTerrain::Mountain;
    definition.cities = {{1.0, 14, 0}};

    lw::Map map;
    cfg.map.width = definition.cols;
    cfg.map.height = definition.rows;
    map.configure(cfg.map);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    ASSERT_TRUE(map.loadFromDefinition(definition));
    EXPECT_FALSE(map.atIndex(0).land);
    EXPECT_TRUE(map.atIndex(13).mountain);
    ASSERT_EQ(map.cityCount(), 1);
    EXPECT_EQ(map.city(0).baseIndex, 14);
    EXPECT_EQ(map.atIndex(14).cityId, 0);
}

TEST(MapDefinition, RandomGenerationInitializesSimulationInMemory) {
    lw::Config cfg = lwtest::loadCfg();
    lw::MapGenParams params{48, 48, 0.35, 0.08, 0.03};
    lw::MapDefinition definition;
    ASSERT_TRUE(lw::MapGenerator::generate(42, params, definition, cfg.city));
    ASSERT_FALSE(definition.terrain.empty());
    cfg.map.width = definition.cols;
    cfg.map.height = definition.rows;
    cfg.map.tiling = lw::tilingName(definition.tiling);
    lw::Simulation sim(cfg, 17, 42);
    sim.setMapDefinition(std::move(definition));
    ASSERT_TRUE(sim.init());
    EXPECT_EQ(sim.map().cellCount(), 48 * 48);
    EXPECT_EQ(sim.map().capitalCount(), 8);
    EXPECT_GE(sim.map().cityCount(), 8);
}

// ---- 河流系统 R2（§5.1）：定义往返 / 缺键 / 非法拒绝 / Map 查询 ----

// 全陆地方形定义（河测试用）。
lw::MapDefinition allLandDefinition(int cols, int rows) {
    lw::MapDefinition definition;
    definition.cols = cols;
    definition.rows = rows;
    definition.terrain.assign(static_cast<std::size_t>(cols) * rows, lw::MapTerrain::Land);
    return definition;
}

TEST(MapDefinition, RiverEdgesRoundTripCanonicalAndSorted) {
    // 乱序输入 → 输出规范升序；(1,3) 是 cell(1,0) 的右邻边。
    lw::MapDefinition definition = allLandDefinition(4, 3);
    definition.rivers = {{1, 3}, {0, 0}, {0, 3}};
    const std::string path = lwtest::testArtifactPath("river_roundtrip.landmap");
    ASSERT_TRUE(definition.saveToFile(path));

    lw::MapDefinition restored;
    std::string error;
    ASSERT_TRUE(lw::MapDefinition::loadFromFile(path, restored, &error)) << error;
    ASSERT_EQ(restored.rivers.size(), 3u);
    EXPECT_EQ(restored.rivers[0], (lw::MapEdgeRef{0, 0}));
    EXPECT_EQ(restored.rivers[1], (lw::MapEdgeRef{0, 3}));
    EXPECT_EQ(restored.rivers[2], (lw::MapEdgeRef{1, 3}));
    // 二次序列化字节稳定（规范化 + 升序 + 去重）。
    EXPECT_EQ(restored.toJson(), definition.toJson());
    std::remove(path.c_str());

    // 从对侧格写的同一条边 → 折到规范侧。
    lw::MapDefinition mirrored = allLandDefinition(4, 3);
    mirrored.rivers = {{1, 2}};  // cell(1,0) 的左邻边 = cell(0,0) 的右邻边
    std::string mirrorError;
    ASSERT_TRUE(mirrored.validate(&mirrorError)) << mirrorError;
    lw::MapDefinition mirroredRestored;
    ASSERT_TRUE(lw::MapDefinition::fromJson(mirrored.toJson(), mirroredRestored, &mirrorError))
        << mirrorError;
    ASSERT_EQ(mirroredRestored.rivers.size(), 1u);
    EXPECT_EQ(mirroredRestored.rivers[0], (lw::MapEdgeRef{0, 3}));
}

TEST(MapDefinition, MissingRiversKeyMeansNoRivers) {
    const std::string json = R"({
      "format": "landwar.map", "version": 2, "tiling": "square",
      "cols": 4, "rows": 3,
      "terrain": ["LLLL", "LLLL", "LLLL"],
      "cities": []
    })";
    lw::MapDefinition definition;
    std::string error;
    ASSERT_TRUE(lw::MapDefinition::fromJson(json, definition, &error)) << error;
    EXPECT_TRUE(definition.rivers.empty());
    // 始终写出（空数组也写，便于人读与 diff）。
    EXPECT_NE(definition.toJson().find("\"rivers\": []"), std::string::npos);
}

TEST(MapDefinition, RejectsInvalidRiverEdges) {
    const auto rejected = [](lw::MapDefinition definition) {
        std::string error;
        const bool ok = definition.validate(&error);
        EXPECT_FALSE(ok) << "expected rejection";
        if (!ok) {
            EXPECT_FALSE(error.empty());
        }
        return !ok && !error.empty();
    };
    {   // 格越界
        lw::MapDefinition d = allLandDefinition(4, 3);
        d.rivers = {{9999, 0}};
        EXPECT_TRUE(rejected(d));
    }
    {   // 边越界
        lw::MapDefinition d = allLandDefinition(4, 3);
        d.rivers = {{0, 99}};
        EXPECT_TRUE(rejected(d));
    }
    {   // 地图边界边（cell0 的下边 / 左边）：河必须两侧都有邻块
        lw::MapDefinition d = allLandDefinition(4, 3);
        d.rivers = {{0, 1}};
        EXPECT_TRUE(rejected(d));
        d.rivers = {{0, 2}};
        EXPECT_TRUE(rejected(d));
    }
    {   // 临海（R5 严格口径）
        lw::MapDefinition d = allLandDefinition(4, 3);
        d.terrain[1] = lw::MapTerrain::Sea;
        d.rivers = {{0, 3}};
        EXPECT_TRUE(rejected(d));
    }
    {   // 重复：同一条边写两次
        lw::MapDefinition d = allLandDefinition(4, 3);
        d.rivers = {{0, 3}, {0, 3}};
        EXPECT_TRUE(rejected(d));
    }
    {   // 重复：两侧格各写一次（折到同一规范边）
        lw::MapDefinition d = allLandDefinition(4, 3);
        d.rivers = {{0, 3}, {1, 2}};
        EXPECT_TRUE(rejected(d));
    }
    {   // JSON 层：rivers 不是数组 / 元素形状非法
        std::string error;
        lw::MapDefinition d;
        EXPECT_FALSE(lw::MapDefinition::fromJson(
            R"({"format":"landwar.map","version":2,"tiling":"square","cols":4,"rows":3,)"
            R"("terrain":["LLLL","LLLL","LLLL"],"cities":[],"rivers":3})",
            d, &error));
        EXPECT_FALSE(lw::MapDefinition::fromJson(
            R"({"format":"landwar.map","version":2,"tiling":"square","cols":4,"rows":3,)"
            R"("terrain":["LLLL","LLLL","LLLL"],"cities":[],"rivers":[[0]]})",
            d, &error));
    }
}

TEST(Map, RiverQueriesFollowDefinition) {
    lw::Config cfg = lwtest::loadCfg();
    lw::MapDefinition definition = allLandDefinition(4, 3);
    definition.rivers = {{0, 3}, {0, 0}};

    lw::Map map;
    map.configureCanonical(lw::TilingType::Square, 4, 3);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    std::string error;
    ASSERT_TRUE(map.loadFromDefinition(definition, &error)) << error;
    // 两条河在两侧格上都能查到。
    EXPECT_TRUE(map.hasRiverEdge(0, 3));
    EXPECT_TRUE(map.hasRiverEdge(1, 2));
    EXPECT_TRUE(map.hasRiverEdge(0, 0));
    EXPECT_TRUE(map.hasRiverEdge(4, 1));
    // 边界边、非河边、越界索引。
    EXPECT_FALSE(map.hasRiverEdge(0, 1));
    EXPECT_FALSE(map.hasRiverEdge(0, 2));
    EXPECT_FALSE(map.hasRiverEdge(9999, 0));
    EXPECT_TRUE(map.hasRiverBetween(0, 1));
    EXPECT_TRUE(map.hasRiverBetween(1, 0));
    EXPECT_TRUE(map.hasRiverBetween(0, 4));
    EXPECT_FALSE(map.hasRiverBetween(1, 2));
    EXPECT_FALSE(map.hasRiverBetween(0, 0));
    ASSERT_EQ(map.riverEdges().size(), 2u);
    EXPECT_EQ(map.riverEdges()[0], (lw::MapEdgeRef{0, 0}));
    EXPECT_EQ(map.riverEdges()[1], (lw::MapEdgeRef{0, 3}));

    // 非法输入不改动已有数据；clear/重配置后清空。
    EXPECT_FALSE(map.setRiversFromDefinition({{0, 1}}, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(map.riverEdges().size(), 2u);
    EXPECT_TRUE(map.hasRiverEdge(0, 3));
    map.configureCanonical(lw::TilingType::Square, 4, 3);
    EXPECT_TRUE(map.riverEdges().empty());
    EXPECT_FALSE(map.hasRiverEdge(0, 3));
}

}  // namespace
