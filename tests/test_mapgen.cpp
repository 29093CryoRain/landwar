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

}  // namespace
