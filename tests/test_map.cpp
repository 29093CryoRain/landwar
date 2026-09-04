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

}  // namespace
