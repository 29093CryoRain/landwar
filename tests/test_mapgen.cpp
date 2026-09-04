#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "core/Config.h"
#include "world/Map.h"
#include "world/MapGenerator.h"
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

}  // namespace
