#include <gtest/gtest.h>

#include <cstdio>

#include "editor/MapEditorModel.h"
#include "TestUtil.h"
#include "world/Map.h"

namespace {

TEST(MapEditorModel, PaintFillResolveAndUndoAreOperationLevel) {
    lw::editor::MapEditorModel model(lw::TilingType::Square, 4, 4, lwtest::loadCfg().city);
    EXPECT_EQ(model.floodFillTerrain(0, lw::MapTerrain::Land), 16);
    EXPECT_EQ(model.floodFillCityMarks(0, true), 16);
    EXPECT_EQ(model.resolvedCities().size(), 6u);
    EXPECT_FALSE(model.hasUnresolvedMarks());
    EXPECT_TRUE(model.undo());
    EXPECT_FALSE(model.cityMarked(0));
    EXPECT_EQ(model.terrainAt(0), lw::MapTerrain::Land);
    EXPECT_EQ(model.resolvedCities().size(), 0u);
    EXPECT_TRUE(model.undo());
    EXPECT_EQ(model.terrainAt(0), lw::MapTerrain::Sea);

    ASSERT_TRUE(model.setCityMark(0));
    EXPECT_EQ(model.terrainAt(0), lw::MapTerrain::City);
    EXPECT_FALSE(model.hasUnresolvedMarks());
}

TEST(MapEditorModel, ChoosesHighestFeasibleConfiguredShapeDeterministically) {
    const lw::Config cfg = lwtest::loadCfg();
    lw::editor::MapEditorModel model(lw::TilingType::Square, 3, 3, cfg.city);
    EXPECT_EQ(model.floodFillTerrain(0, lw::MapTerrain::Land), 9);
    for (int i = 0; i < model.cellCount(); ++i) EXPECT_TRUE(model.setCityMark(i, true));
    ASSERT_FALSE(model.resolvedCities().empty());
    EXPECT_DOUBLE_EQ(model.resolvedCities().front().level, 9.0);
    EXPECT_EQ(model.resolvedCities().front().baseIndex, 0);
    EXPECT_FALSE(model.hasUnresolvedMarks());
}

TEST(MapEditorModel, NativeExportContainsTerrainAndResolvedCities) {
    lw::editor::MapEditorModel model(lw::TilingType::Square, 2, 2);
    ASSERT_TRUE(model.paintCell(0, lw::MapTerrain::Land));
    ASSERT_TRUE(model.setCityMark(0));
    const std::string path = lwtest::testArtifactPath("editor_export.landmap");
    ASSERT_TRUE(model.saveToFile(path));
    lw::MapDefinition loaded;
    std::string error;
    ASSERT_TRUE(lw::MapDefinition::loadFromFile(path, loaded, &error)) << error;
    EXPECT_EQ(loaded.terrain, model.toDefinition().terrain);
    EXPECT_EQ(loaded.cities.size(), model.resolvedCities().size());
    std::remove(path.c_str());
}

TEST(MapEditorModel, LargeCityOperationIsMeasuredAndCityTerrainIsExported) {
    lw::editor::MapEditorModel model(lw::TilingType::Square, 8, 4, lwtest::loadCfg().city);
    ASSERT_EQ(model.floodFillCityMarks(0, true), 32);
    EXPECT_EQ(model.lastCityMarkIncrease(), 32);
    EXPECT_FALSE(model.hasUnresolvedMarks());

    const lw::MapDefinition definition = model.toDefinition();
    ASSERT_EQ(definition.terrain.size(), 32u);
    for (const auto terrain : definition.terrain) EXPECT_EQ(terrain, lw::MapTerrain::City);
}

TEST(MapEditorModel, CoastalMountainsAreReportedAndRemainMountainOnExport) {
    lw::editor::MapEditorModel model(lw::TilingType::Square, 3, 3);
    ASSERT_TRUE(model.paintCell(4, lw::MapTerrain::Mountain));
    EXPECT_EQ(model.mountainCoastViolationCount(), 1);
    const lw::MapDefinition definition = model.toDefinition();
    EXPECT_EQ(definition.terrain[4], lw::MapTerrain::Mountain);
}

TEST(MapEditorModel, UnresolvedCityMarksRemainCityOnExport) {
    lw::Config::City cityConfig;
    cityConfig.square.levels = {2.0};
    cityConfig.square.shapes.resize(1);
    cityConfig.square.shapes[0].cells = {{0.0, 0.0}, {1.0, 0.0}};
    cityConfig.square.shapeLevelIndex = {0};

    lw::editor::MapEditorModel model(lw::TilingType::Square, 2, 2, cityConfig);
    ASSERT_TRUE(model.setCityMark(0));
    ASSERT_TRUE(model.hasUnresolvedMarks());
    EXPECT_EQ(model.toDefinition().terrain[0], lw::MapTerrain::City);
}

TEST(MapEditorModel, CityAndMountainShareCellAndSurviveExport) {
    const lw::Config cfg = lwtest::loadCfg();
    lw::editor::MapEditorModel model(lw::TilingType::Square, 2, 2, cfg.city);
    ASSERT_TRUE(model.paintCell(0, lw::MapTerrain::Mountain));
    ASSERT_TRUE(model.setCityMark(0));
    EXPECT_EQ(model.terrainAt(0), lw::MapTerrain::City);
    EXPECT_TRUE(model.mountainMarked(0));
    ASSERT_GE(model.resolvedCityIdAt(0), 0);

    const lw::MapDefinition definition = model.toDefinition();
    EXPECT_EQ(definition.terrain[0], lw::MapTerrain::Mountain);
    ASSERT_EQ(definition.cities.size(), 1u);

    lw::Map map;
    map.configureCanonical(lw::TilingType::Square, 2, 2);
    map.setCityConfig(cfg.city);
    std::string error;
    ASSERT_TRUE(map.loadFromDefinition(definition, &error)) << error;
    EXPECT_TRUE(map.atIndex(0).mountain);
    EXPECT_GE(map.atIndex(0).cityId, 0);
}

}  // namespace
