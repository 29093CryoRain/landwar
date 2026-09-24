#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

#include "editor/MapEditorModel.h"
#include "TestUtil.h"
#include "world/Map.h"

namespace {

std::vector<int> directShapeCells(const lw::TilingGeom& geometry,
                                  const lw::Config::City::Shape& shape, int anchor) {
    double ax = 0.0, ay = 0.0;
    geometry.cellCenter(anchor, ax, ay);
    std::vector<int> cells;
    cells.reserve(shape.cells.size());
    for (const auto& offset : shape.cells)
        cells.push_back(geometry.worldToCell(ax + offset.dx, ay + offset.dy));
    return cells;
}

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
    EXPECT_EQ(model.resolvedCities().front().baseIndex, 4);
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

// ---- 河流工具（河流系统 §8.1/§8.2，R5）----

// 4×4 全陆地：格 0=(0,0)，边 3=右邻格 1，边 0=上邻格 4，边 1/2 是地图边界边。
lw::editor::MapEditorModel riverModel() {
    lw::editor::MapEditorModel model(lw::TilingType::Square, 4, 4);
    EXPECT_EQ(model.floodFillTerrain(0, lw::MapTerrain::Land), 16);
    return model;
}

TEST(MapEditorModel, RiverSetRejectsBoundarySeaAndOutOfRange) {
    lw::editor::MapEditorModel model = riverModel();
    // 合法：内部边
    EXPECT_TRUE(model.setRiver(0, 3));
    EXPECT_TRUE(model.riverMarked(0, 3));
    EXPECT_TRUE(model.riverMarked(1, 2));  // 对侧格同一条边
    EXPECT_FALSE(model.setRiver(0, 3));    // 重复置 → no-op
    EXPECT_EQ(model.rivers().size(), 1u);
    // 地图边界边（格 0 的下边/左边）拒绝
    EXPECT_FALSE(model.setRiver(0, 1));
    EXPECT_FALSE(model.setRiver(0, 2));
    // 越界拒绝
    EXPECT_FALSE(model.setRiver(-1, 0));
    EXPECT_FALSE(model.setRiver(0, 99));
    EXPECT_FALSE(model.setRiver(model.cellCount(), 0));
    // 临海拒绝（把格 1 改成海 → 边 (0,3) 变成违规项，但仍保留可见标记）
    ASSERT_TRUE(model.paintCell(1, lw::MapTerrain::Sea));
    EXPECT_TRUE(model.riverMarked(0, 3));
    EXPECT_TRUE(model.hasRiverViolations());
    EXPECT_EQ(model.riverSeaViolationCount(), 1);
    EXPECT_TRUE(model.setRiver(0, 0));             // 另一条内部边（邻格 4）仍合法
    EXPECT_EQ(model.rivers().size(), 2u);
    EXPECT_FALSE(model.setRiver(1, 0));            // 格 1 自身是海 → 它的边一律临海
    EXPECT_EQ(model.riverSeaViolationCount(), 1);  // 只有 (0,3) 违规
}

TEST(MapEditorModel, RiverViolationsAreNotExportedButRemainVisible) {
    lw::editor::MapEditorModel model = riverModel();
    ASSERT_TRUE(model.setRiver(0, 3));   // 与格 1 相邻
    ASSERT_TRUE(model.setRiver(0, 0));   // 与格 4 相邻
    ASSERT_TRUE(model.paintCell(1, lw::MapTerrain::Sea));
    ASSERT_EQ(model.riverSeaViolationCount(), 1);
    EXPECT_EQ(model.rivers().size(), 2u);          // 两条都还在（可见标记）
    lw::MapDefinition definition = model.toDefinition();
    ASSERT_EQ(definition.rivers.size(), 1u);       // 非法项不导出
    EXPECT_EQ(definition.rivers.front(), (lw::MapEdgeRef{0, 0}));
    // 地形改回陆地 → 违规自动消失，导出两条。
    ASSERT_TRUE(model.paintCell(1, lw::MapTerrain::Land));
    EXPECT_FALSE(model.hasRiverViolations());
    EXPECT_EQ(model.toDefinition().rivers.size(), 2u);
}

TEST(MapEditorModel, RiverUndoRestoresPreviousEdgeSet) {
    lw::editor::MapEditorModel model = riverModel();
    ASSERT_TRUE(model.setRiver(0, 3));
    ASSERT_TRUE(model.setRiver(0, 0));
    EXPECT_EQ(model.rivers().size(), 2u);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.rivers().size(), 1u);
    EXPECT_TRUE(model.riverMarked(0, 3));
    EXPECT_FALSE(model.riverMarked(0, 0));
    ASSERT_TRUE(model.undo());
    EXPECT_TRUE(model.rivers().empty());
    // 地形改动导致的违规也随 undo 复原。
    ASSERT_TRUE(model.setRiver(0, 3));
    ASSERT_TRUE(model.paintCell(1, lw::MapTerrain::Sea));
    EXPECT_EQ(model.riverSeaViolationCount(), 1);
    ASSERT_TRUE(model.undo());
    EXPECT_FALSE(model.hasRiverViolations());
}

TEST(MapEditorModel, RiverPaintNearestUsesDistanceThreshold) {
    lw::editor::MapEditorModel model = riverModel();
    const lw::TilingGeom& g = model.geometry();
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    ASSERT_TRUE(g.cellEdge(0, 3, x0, y0, x1, y1));  // 格 0 的右边 x=1，y∈[0,1]
    // 靠近该边 → 命中"最近边"并画上（两条格谁持有都折到同一条规范边）
    EXPECT_EQ(model.paintRiverNearest(0, 0.98, 0.5), 1);
    EXPECT_TRUE(model.riverMarked(0, 3));
    EXPECT_EQ(model.paintRiverNearest(0, 0.98, 0.5), 0);  // 已存在 → no-op
    // 从对侧格、靠近同一条边 → 仍是同一条规范边（no-op）
    EXPECT_EQ(model.paintRiverNearest(1, 1.02, 0.5), 0);
    EXPECT_TRUE(model.riverMarked(1, 2));
    // 擦河：靠近同一条边 → 擦掉
    EXPECT_EQ(model.eraseRiverNearest(1, 1.02, 0.5), 1);
    EXPECT_FALSE(model.riverMarked(0, 3));
    // 拖拽 batch：一条拖拽 = 一次撤销
    model.beginBatch();
    model.paintRiverNearest(1, 2.02, 0.5);  // 格 1 右边（邻格 2）
    model.paintRiverNearest(2, 3.02, 0.5);  // 格 2 右边（邻格 3）
    ASSERT_TRUE(model.endBatch());
    EXPECT_EQ(model.rivers().size(), 2u);
    ASSERT_TRUE(model.undo());
    EXPECT_TRUE(model.rivers().empty());
}

TEST(MapEditorModel, RiverPaintNearestThresholdRejectsHexCenter) {
    // 六边形内切圆半径 ≈ 0.537 U > 0.5 U：格中心到最近边太远 → 必须忽略（§8.2 阈值）。
    lw::editor::MapEditorModel model(lw::TilingType::Hex, 4, 4);
    ASSERT_EQ(model.floodFillTerrain(0, lw::MapTerrain::Land), model.cellCount());
    const int cell = model.geometry().cellIndexAt(2, 2, 0);
    ASSERT_GE(cell, 0);
    double cx = 0.0, cy = 0.0;
    model.geometry().cellCenter(cell, cx, cy);
    EXPECT_EQ(model.paintRiverNearest(cell, cx, cy), 0);
    EXPECT_TRUE(model.rivers().empty());
    // 靠近某条边（中心朝邻格方向 40% 距离）→ 命中该边
    int edge = -1;
    for (int k = 0; k < model.geometry().neighborCount(cell); ++k)
        if (model.geometry().neighbor(cell, k) >= 0) {
            edge = k;
            break;
        }
    ASSERT_GE(edge, 0);
    const int nb = model.geometry().neighbor(cell, edge);
    double nx = 0.0, ny = 0.0;
    model.geometry().cellCenter(nb, nx, ny);
    const double px = cx + 0.4 * (nx - cx), py = cy + 0.4 * (ny - cy);
    EXPECT_EQ(model.paintRiverNearest(cell, px, py), 1);
    EXPECT_TRUE(model.riverMarked(cell, edge));
}

TEST(MapEditorModel, RiverLoadExportRoundTripKeepsLegalEdges) {
    lw::editor::MapEditorModel model = riverModel();
    ASSERT_TRUE(model.setRiver(0, 3));
    ASSERT_TRUE(model.setRiver(0, 0));
    const lw::MapDefinition definition = model.toDefinition();

    lw::editor::MapEditorModel reloaded(lw::TilingType::Square, 4, 4);
    std::string error;
    ASSERT_TRUE(reloaded.loadDefinition(definition, &error)) << error;
    EXPECT_TRUE(reloaded.riverMarked(0, 3));
    EXPECT_TRUE(reloaded.riverMarked(0, 0));
    EXPECT_FALSE(reloaded.hasRiverViolations());
    EXPECT_EQ(reloaded.toDefinition().rivers, definition.rivers);
}

TEST(MapEditorModel, RiverLoadKeepsIllegalEdgesAsVisibleMarks) {
    // 直接构造"合法定义 + 手工把一条河改成临海"：编辑器宽松载入（validate 会拒，故先清 rivers
    // 校验其余字段的语义在此由 loadDefinition 内部处理）。
    lw::editor::MapEditorModel model = riverModel();
    ASSERT_TRUE(model.setRiver(0, 3));
    lw::MapDefinition definition = model.toDefinition();
    definition.terrain[0] = lw::MapTerrain::Sea;  // 让该河临海（终态非法）
    lw::editor::MapEditorModel reloaded(lw::TilingType::Square, 4, 4);
    std::string error;
    ASSERT_TRUE(reloaded.loadDefinition(definition, &error)) << error;
    EXPECT_TRUE(reloaded.riverMarked(0, 3));      // 保留可见标记（宽松载入）
    EXPECT_EQ(reloaded.riverSeaViolationCount(), 1);
    EXPECT_TRUE(reloaded.toDefinition().rivers.empty());  // 不导出
}

TEST(MapEditorModel, HexCityResolutionUsesPeriodicBlockCoordinates) {
    const lw::Config cfg = lwtest::loadCfg();
    lw::editor::MapEditorModel model(lw::TilingType::Hex, 24, 14, cfg.city);
    ASSERT_EQ(model.floodFillTerrain(0, lw::MapTerrain::Land), model.cellCount());

    const auto* shape = cfg.city.hex.shapeFor(3.0, 0);
    ASSERT_NE(shape, nullptr);
    const int anchor = model.geometry().cellIndexAt(6, 10, 0);
    const std::vector<int> expected = directShapeCells(model.geometry(), *shape, anchor);
    ASSERT_EQ(expected.size(), 3u);
    for (int index : expected) ASSERT_TRUE(model.setCityMark(index));

    ASSERT_EQ(model.resolvedCities().size(), 1u);
    EXPECT_EQ(model.resolvedCities()[0].baseIndex, anchor);
    EXPECT_FALSE(model.hasUnresolvedMarks());

    const std::string path = lwtest::testArtifactPath("editor_hex_city.landmap");
    std::string error;
    ASSERT_TRUE(model.saveToFile(path, &error)) << error;
    lw::Map map;
    map.setCityConfig(cfg.city);
    ASSERT_TRUE(map.loadFromLandmap(path, &error)) << error;
    EXPECT_EQ(map.totalCities(), 1);
    std::remove(path.c_str());
}

TEST(MapEditorModel, TriCityResolutionUsesPeriodicBlockCoordinates) {
    const lw::Config cfg = lwtest::loadCfg();
    lw::editor::MapEditorModel model(lw::TilingType::Tri, 24, 14, cfg.city);
    ASSERT_EQ(model.floodFillTerrain(0, lw::MapTerrain::Land), model.cellCount());

    const auto* shape = cfg.city.tri.shapeFor(4.0, 1);
    ASSERT_NE(shape, nullptr);
    const int anchor = model.geometry().cellIndexAt(6, 10, 0);
    const std::vector<int> expected = directShapeCells(model.geometry(), *shape, anchor);
    ASSERT_EQ(expected.size(), 4u);
    for (int index : expected) ASSERT_TRUE(model.setCityMark(index));

    ASSERT_EQ(model.resolvedCities().size(), 1u);
    EXPECT_EQ(model.resolvedCities()[0].baseIndex, anchor);
    EXPECT_FALSE(model.hasUnresolvedMarks());

    const std::string path = lwtest::testArtifactPath("editor_tri_city.landmap");
    std::string error;
    ASSERT_TRUE(model.saveToFile(path, &error)) << error;
    lw::Map map;
    map.setCityConfig(cfg.city);
    ASSERT_TRUE(map.loadFromLandmap(path, &error)) << error;
    EXPECT_EQ(map.totalCities(), 1);
    std::remove(path.c_str());
}

TEST(MapEditorModel, TriShapeOrientationsUseExplicitVariants) {
    const lw::Config cfg = lwtest::loadCfg();
    lw::Map map;
    map.configureCanonical(lw::TilingType::Tri, 24, 14);
    map.setCityConfig(cfg.city);
    for (int index = 0; index < map.cellCount(); ++index) {
        map.atIndex(index).land = true;
        map.atIndex(index).cityAllowed = true;
    }
    const int up = map.geom().cellIndexAt(6, 10, 0);
    const int down = map.geom().cellIndexAt(6, 10, 1);
    EXPECT_TRUE(map.canPlaceCity(4.0, up));
    EXPECT_TRUE(map.canPlaceCity(4.0, down));
    EXPECT_EQ(cfg.city.tri.variantCount(4.0), 2);
}

}  // namespace
