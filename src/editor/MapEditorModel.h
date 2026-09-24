#pragma once

#include <string>
#include <vector>

#include "core/Config.h"
#include "world/MapDefinition.h"
#include "world/tiling/Tiling.h"

namespace lw::editor {

// Editable terrain. City is a first-class terrain value; resolved city ids are
// derived records for preview/export and never become runtime Map ids.
struct MapEditorCell {
    MapTerrain terrain = MapTerrain::Sea;
    bool mountain = false;

    bool operator==(const MapEditorCell&) const = default;
};

class MapEditorModel {
public:
    MapEditorModel() = default;
    MapEditorModel(TilingType tiling, int cols, int rows,
                   const Config::City& cityConfig = Config::City{});

    // Dimensions here are already the native/canonical domain dimensions.
    bool configureCanonical(TilingType tiling, int cols, int rows,
                            std::string* err = nullptr);
    bool loadDefinition(const MapDefinition& definition, std::string* err = nullptr);
    bool loadFromFile(const std::string& path, std::string* err = nullptr);
    void setCityConfig(const Config::City& cityConfig);
    const Config::City& cityConfig() const { return cityConfig_; }

    const TilingGeom& geometry() const { return geometry_; }
    TilingType tiling() const { return geometry_.type; }
    int cols() const { return geometry_.cols; }
    int rows() const { return geometry_.rows; }
    int cellCount() const { return geometry_.cellCount(); }

    const MapEditorCell& cell(int index) const;
    MapTerrain terrainAt(int index) const;
    bool cityMarked(int index) const;
    bool mountainMarked(int index) const;
    int resolvedCityIdAt(int index) const;
    const std::vector<MapCityDefinition>& resolvedCities() const { return resolvedCities_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    bool hasUnresolvedMarks() const { return !unresolvedMarks_.empty(); }
    int unresolvedMarkCount() const { return static_cast<int>(unresolvedMarks_.size()); }
    bool hasMountainCoastViolations() const {
        ensureMountainCoastViolations();
        return !mountainCoastViolations_.empty();
    }
    int mountainCoastViolationCount() const {
        ensureMountainCoastViolations();
        return static_cast<int>(mountainCoastViolations_.size());
    }

    // ---- 河流（河流系统 §8.1）：河是**边**属性，用"离点击位置最近的边"落笔 ----
    bool riverMarked(int cell, int edge) const;
    // 一次可撤销操作：置/清一条河边。置河要求"界内 + 非地图边界边 + 两侧地形都不是海"，
    // 不合法时拒绝并写入 warnings（不产生 undo 记录）。
    bool setRiver(int cell, int edge, bool marked = true);
    // 画/擦"离世界点 (wx,wy) 最近的边"：返回 0/1（是否改变）。命中太远（> 0.5 格）忽略。
    int paintRiverNearest(int cell, double wx, double wy);
    int eraseRiverNearest(int cell, double wx, double wy);
    // 规范 (cell,edge)、升序、去重；**含非法项**（地形被改成海后仍保留可见标记）。
    const std::vector<MapEdgeRef>& rivers() const { return rivers_; }
    // 非法河（临海/地图边界边）：与 mountainCoastViolation 同型（懒计算）。
    const std::vector<MapEdgeRef>& riverViolations() const {
        ensureRiverViolations();
        return riverViolations_;
    }
    bool hasRiverViolations() const { return !riverViolations().empty(); }
    int riverSeaViolationCount() const { return static_cast<int>(riverViolations().size()); }

    // Each successful call is one undoable operation. No-op calls do not add history.
    bool paintCell(int index, MapTerrain terrain);
    bool setCityMark(int index, bool marked = true);
    int floodFillTerrain(int index, MapTerrain terrain);
    int floodFillCityMarks(int index, bool marked = true);
    // Group a drag stroke into one undoable operation. City resolution remains
    // immediate for every mutation inside the batch.
    void beginBatch();
    bool endBatch();
    bool batchActive() const { return batchActive_; }
    bool undo();
    bool canUndo() const { return !undo_.empty(); }
    // Number of city marks added by the most recent successful operation.
    // The editor uses this for the accidental large-operation confirmation.
    int lastCityMarkIncrease() const { return lastCityMarkIncrease_; }

    // Re-resolve all marks using the configured city shape table. This is useful
    // after changing a configuration and is also called by every edit operation.
    void resolveCities();

    // Returns fully native data. Unresolved city marks remain city terrain and
    // are reported through warnings(), so the editor's visible intent is kept.
    MapDefinition toDefinition() const;
    MapDefinition exportDefinition() const { return toDefinition(); }
    bool saveToFile(const std::string& path, std::string* err = nullptr) const;

private:
    struct State {
        std::vector<MapEditorCell> cells;
        std::vector<MapCityDefinition> cities;
        std::vector<int> cityIds;
        std::vector<int> unresolved;
        std::vector<int> mountainCoastViolations;
        std::vector<std::string> warnings;
        std::vector<MapEdgeRef> rivers;
        std::vector<MapEdgeRef> riverViolations;
    };

    struct ShapeCellOffset {
        int dr = 0;
        int dc = 0;
        int base = 0;
    };

    struct ShapeOffsetCacheEntry {
        double level = 0.0;
        int variant = 0;
        int anchorBase = 0;
        std::vector<ShapeCellOffset> cells;
    };

    struct ResolvedComponent {
        std::vector<int> cells;
        std::vector<MapCityDefinition> cities;
        std::vector<std::vector<int>> cityCells;
        std::vector<int> unresolved;
    };

    bool validIndex(int index) const;
    bool applyCell(int index, const MapEditorCell& next);
    // (cell,edge) 折到规范侧（越界/无邻居时原样返回）。
    MapEdgeRef canonicalRiverRef(const MapEdgeRef& ref) const;
    // 合法河边：界内 ∧ 非地图边界边 ∧ 两侧地形都不是海。
    bool riverEdgeLegal(const MapEdgeRef& ref) const;
    // 离 (wx,wy) 最近的**非地图边界**边序号；无 → -1。outDistance = 点到线段的距离（世界单位）。
    int nearestRiverEdge(int cell, double wx, double wy, double& outDistance) const;
    void rebuildRiverViolations() const;
    void ensureRiverViolations() const;
    State snapshot() const;
    void restore(State state);
    void beginOperation(const State& before);
    void rebuildResolution();
    void rebuildResolution(const std::vector<int>& changedIndices);
    ResolvedComponent resolveComponent(const std::vector<int>& component) const;
    void rebuildResolvedViews();
    void rebuildMountainCoastViolations() const;
    void ensureMountainCoastViolations() const;
    std::vector<int> markedComponent(int start, std::vector<bool>& seen) const;
    std::vector<int> shapeCells(double level, int anchor, int variant) const;

    TilingGeom geometry_;
    Config::City cityConfig_;
    std::vector<MapEditorCell> cells_;
    std::vector<MapCityDefinition> resolvedCities_;
    std::vector<int> resolvedCityIds_;
    std::vector<int> unresolvedMarks_;
    mutable std::vector<int> mountainCoastViolations_;
    mutable bool mountainCoastViolationsDirty_ = true;
    std::vector<MapEdgeRef> rivers_;                  // 规范、升序、去重（含非法项）
    mutable std::vector<MapEdgeRef> riverViolations_;  // 非法项（临海/边界边）
    mutable bool riverViolationsDirty_ = true;
    std::vector<std::string> warnings_;
    std::vector<State> undo_;
    mutable std::vector<ShapeOffsetCacheEntry> shapeOffsetCache_;
    std::vector<ResolvedComponent> resolutionCache_;
    int lastCityMarkIncrease_ = 0;
    State batchBefore_;
    bool batchActive_ = false;
    bool batchChanged_ = false;
    int batchCityMarkIncrease_ = 0;
};

}  // namespace lw::editor

namespace lw {
using MapEditorCell = editor::MapEditorCell;
using MapEditorModel = editor::MapEditorModel;
}  // namespace lw
