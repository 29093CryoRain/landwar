#include "editor/MapEditorModel.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <unordered_set>

namespace lw::editor {

MapEditorModel::MapEditorModel(TilingType tiling, int cols, int rows,
                               const Config::City& cityConfig)
    : cityConfig_(cityConfig) {
    configureCanonical(tiling, cols, rows);
}

bool MapEditorModel::configureCanonical(TilingType tiling, int cols, int rows, std::string* err) {
    if (static_cast<int>(tiling) < 0 || static_cast<int>(tiling) >= kTilingTypeCount || cols <= 0 ||
        rows <= 0) {
        if (err) *err = "editor dimensions must be positive";
        return false;
    }
    const TilingGeom geometry{tiling, cols, rows};
    if (geometry.cellCount() <= 0) {
        if (err) *err = "editor geometry has no cells";
        return false;
    }
    geometry_ = geometry;
    cells_.assign(static_cast<std::size_t>(geometry_.cellCount()), MapEditorCell{});
    resolvedCities_.clear();
    resolvedCityIds_.assign(cells_.size(), -1);
    unresolvedMarks_.clear();
    mountainCoastViolations_.clear();
    mountainCoastViolationsDirty_ = true;
    warnings_.clear();
    undo_.clear();
    lastCityMarkIncrease_ = 0;
    batchBefore_ = State{};
    batchActive_ = false;
    batchChanged_ = false;
    batchCityMarkIncrease_ = 0;
    shapeOffsetCache_.clear();
    resolutionCache_.clear();
    return true;
}

bool MapEditorModel::loadDefinition(const MapDefinition& definition, std::string* err) {
    if (!definition.validate(err)) return false;
    if (!configureCanonical(definition.tiling, definition.cols, definition.rows, err)) return false;
    for (std::size_t i = 0; i < definition.terrain.size(); ++i)
        cells_[i].terrain = definition.terrain[i];

    // Native cities become marks by expanding their configured shape. The
    // imported resolved records are retained only when they can be expanded.
    for (const auto& city : definition.cities) {
        const std::vector<int> occupied = shapeCells(city.level, city.baseIndex, city.shapeVariant);
        if (occupied.empty()) {
            warnings_.push_back("city record could not be converted to editor marks");
            continue;
        }
        bool valid = true;
        for (int index : occupied) {
            if (!validIndex(index)) {
                valid = false;
                break;
            }
            cells_[static_cast<std::size_t>(index)].terrain = MapTerrain::City;
        }
        if (!valid) warnings_.push_back("city record has cells outside the editor geometry");
    }
    mountainCoastViolationsDirty_ = true;
    rebuildResolution();
    return true;
}

bool MapEditorModel::loadFromFile(const std::string& path, std::string* err) {
    MapDefinition definition;
    if (!MapDefinition::loadFromFile(path, definition, err)) return false;
    return loadDefinition(definition, err);
}

void MapEditorModel::setCityConfig(const Config::City& cityConfig) {
    cityConfig_ = cityConfig;
    shapeOffsetCache_.clear();
    resolutionCache_.clear();
    undo_.clear();
    if (!cells_.empty()) rebuildResolution();
}

const MapEditorCell& MapEditorModel::cell(int index) const {
    static const MapEditorCell invalid{};
    return validIndex(index) ? cells_[static_cast<std::size_t>(index)] : invalid;
}

MapTerrain MapEditorModel::terrainAt(int index) const { return cell(index).terrain; }

bool MapEditorModel::cityMarked(int index) const {
    return validIndex(index) && cell(index).terrain == MapTerrain::City;
}

int MapEditorModel::resolvedCityIdAt(int index) const {
    return validIndex(index) ? resolvedCityIds_[static_cast<std::size_t>(index)] : -1;
}

bool MapEditorModel::validIndex(int index) const { return index >= 0 && index < cellCount(); }

MapEditorModel::State MapEditorModel::snapshot() const {
    ensureMountainCoastViolations();
    return {cells_, resolvedCities_, resolvedCityIds_, unresolvedMarks_, mountainCoastViolations_,
            warnings_};
}

void MapEditorModel::restore(State state) {
    cells_ = std::move(state.cells);
    resolvedCities_ = std::move(state.cities);
    resolvedCityIds_ = std::move(state.cityIds);
    unresolvedMarks_ = std::move(state.unresolved);
    mountainCoastViolations_ = std::move(state.mountainCoastViolations);
    mountainCoastViolationsDirty_ = false;
    warnings_ = std::move(state.warnings);
    resolutionCache_.clear();
}

void MapEditorModel::beginOperation(const State& before) { undo_.push_back(before); }

void MapEditorModel::rebuildMountainCoastViolations() const {
    mountainCoastViolations_.clear();
    for (int index = 0; index < cellCount(); ++index) {
        if (terrainAt(index) != MapTerrain::Mountain) continue;
        bool nearSea = false;
        for (int k = 0; k < geometry_.pointNeighborCount(index); ++k) {
            const int neighbor = geometry_.pointNeighbor(index, k);
            if (neighbor >= 0 && terrainAt(neighbor) == MapTerrain::Sea) {
                nearSea = true;
                break;
            }
        }
        if (nearSea) mountainCoastViolations_.push_back(index);
    }
    mountainCoastViolationsDirty_ = false;
}

void MapEditorModel::ensureMountainCoastViolations() const {
    if (mountainCoastViolationsDirty_) rebuildMountainCoastViolations();
}

bool MapEditorModel::paintCell(int index, MapTerrain terrain) {
    if (!validIndex(index) || cells_[static_cast<std::size_t>(index)].terrain == terrain) return false;
    const State before = batchActive_ ? State{} : snapshot();
    const bool cityStateAffected = cells_[static_cast<std::size_t>(index)].terrain == MapTerrain::City
                                   || terrain == MapTerrain::City;
    const bool addingCity = terrain == MapTerrain::City;
    cells_[static_cast<std::size_t>(index)].terrain = terrain;
    mountainCoastViolationsDirty_ = true;
    if (cityStateAffected) rebuildResolution();
    if (batchActive_) {
        batchChanged_ = true;
        if (addingCity) ++batchCityMarkIncrease_;
    } else {
        lastCityMarkIncrease_ = addingCity ? 1 : 0;
        beginOperation(before);
    }
    return true;
}

bool MapEditorModel::setCityMark(int index, bool marked) {
    if (!validIndex(index) || cityMarked(index) == marked) return false;
    return paintCell(index, marked ? MapTerrain::City : MapTerrain::Land);
}

int MapEditorModel::floodFillTerrain(int index, MapTerrain terrain) {
    if (!validIndex(index)) return 0;
    const MapTerrain original = terrainAt(index);
    if (original == terrain) return 0;
    const State before = batchActive_ ? State{} : snapshot();
    std::vector<int> todo{index};
    std::vector<bool> seen(cells_.size(), false);
    int changed = 0;
    while (!todo.empty()) {
        const int current = todo.back();
        todo.pop_back();
        if (!validIndex(current) || seen[static_cast<std::size_t>(current)] ||
            terrainAt(current) != original)
            continue;
        seen[static_cast<std::size_t>(current)] = true;
        cells_[static_cast<std::size_t>(current)].terrain = terrain;
        ++changed;
        for (int k = 0; k < geometry_.neighborCount(current); ++k) {
            const int next = geometry_.neighbor(current, k);
            if (validIndex(next) && !seen[static_cast<std::size_t>(next)]) todo.push_back(next);
        }
    }
    if (changed > 0) {
        mountainCoastViolationsDirty_ = true;
        const bool cityStateAffected = original == MapTerrain::City || terrain == MapTerrain::City;
        if (cityStateAffected) rebuildResolution();
        if (batchActive_) {
            batchChanged_ = true;
            if (terrain == MapTerrain::City) batchCityMarkIncrease_ += changed;
        } else {
            lastCityMarkIncrease_ = terrain == MapTerrain::City ? changed : 0;
            beginOperation(before);
        }
    }
    return changed;
}

int MapEditorModel::floodFillCityMarks(int index, bool marked) {
    if (!validIndex(index)) return 0;
    const MapTerrain original = terrainAt(index);
    const MapTerrain target = marked ? MapTerrain::City : MapTerrain::Land;
    if (original == target) return 0;
    return floodFillTerrain(index, target);
}

void MapEditorModel::beginBatch() {
    if (batchActive_) return;
    batchBefore_ = snapshot();
    batchActive_ = true;
    batchChanged_ = false;
    batchCityMarkIncrease_ = 0;
    lastCityMarkIncrease_ = 0;
}

bool MapEditorModel::endBatch() {
    if (!batchActive_) return false;
    const bool changed = batchChanged_;
    if (changed) undo_.push_back(std::move(batchBefore_));
    batchBefore_ = State{};
    batchActive_ = false;
    batchChanged_ = false;
    lastCityMarkIncrease_ = batchCityMarkIncrease_;
    batchCityMarkIncrease_ = 0;
    return changed;
}

bool MapEditorModel::undo() {
    if (batchActive_) endBatch();
    if (undo_.empty()) return false;
    State previous = std::move(undo_.back());
    undo_.pop_back();
    restore(std::move(previous));
    lastCityMarkIncrease_ = 0;
    return true;
}

std::vector<int> MapEditorModel::markedComponent(int start, std::vector<bool>& seen) const {
    std::vector<int> component;
    if (!validIndex(start) || !cityMarked(start) || seen[static_cast<std::size_t>(start)]) return component;
    std::vector<int> todo{start};
    seen[static_cast<std::size_t>(start)] = true;
    while (!todo.empty()) {
        const int current = todo.back();
        todo.pop_back();
        component.push_back(current);
        for (int k = 0; k < geometry_.neighborCount(current); ++k) {
            const int next = geometry_.neighbor(current, k);
            if (validIndex(next) && cityMarked(next) && !seen[static_cast<std::size_t>(next)]) {
                seen[static_cast<std::size_t>(next)] = true;
                todo.push_back(next);
            }
        }
    }
    std::sort(component.begin(), component.end());
    return component;
}

std::vector<int> MapEditorModel::shapeCells(double level, int anchor, int variant) const {
    std::vector<int> out;
    if (!validIndex(anchor)) return out;
    const auto& set = cityConfig_.setFor(geometry_.type);
    const Config::City::Shape* shape = set.shapeFor(level, variant);
    if (!shape) return out;
    int anchorRow = 0, anchorCol = 0, anchorBase = 0;
    geometry_.indexToRowCol(anchor, anchorRow, anchorCol, anchorBase);
    const auto cached = std::find_if(
        shapeOffsetCache_.begin(), shapeOffsetCache_.end(), [&](const ShapeOffsetCacheEntry& entry) {
            return entry.level == level && entry.variant == variant
                   && entry.anchorBase == anchorBase;
        });
    if (cached == shapeOffsetCache_.end()) {
        ShapeOffsetCacheEntry entry;
        entry.level = level;
        entry.variant = variant;
        entry.anchorBase = anchorBase;

        // Resolve each configured world offset once at a central anchor. The
        // resulting row/column/base offsets are translation-invariant within a
        // periodic tiling and avoid repeated worldToCell fallback scans while
        // the editor searches city candidates.
        const int sampleRow = geometry_.rows / 2;
        const int sampleCol = geometry_.cols / 2;
        const int sampleAnchor = geometry_.cellIndexAt(sampleRow, sampleCol, anchorBase);
        double ax = 0.0, ay = 0.0;
        geometry_.cellCenter(sampleAnchor, ax, ay);
        const bool anchorUp = geometry_.type != TilingType::Tri || (sampleAnchor % 2 == 0);
        bool valid = sampleAnchor >= 0;
        for (const auto& cell : shape->cells) {
            if (!valid) break;
            const int target = geometry_.worldToCell(ax + cell.dx,
                                                    ay + (anchorUp ? cell.dy : -cell.dy));
            if (target < 0) {
                valid = false;
                break;
            }
            int targetRow = 0, targetCol = 0, targetBase = 0;
            geometry_.indexToRowCol(target, targetRow, targetCol, targetBase);
            entry.cells.push_back({targetRow - sampleRow, targetCol - sampleCol, targetBase});
        }
        if (valid && entry.cells.size() == shape->cells.size()) {
            shapeOffsetCache_.push_back(std::move(entry));
        }
    }

    const auto resolved = std::find_if(
        shapeOffsetCache_.begin(), shapeOffsetCache_.end(), [&](const ShapeOffsetCacheEntry& entry) {
            return entry.level == level && entry.variant == variant
                   && entry.anchorBase == anchorBase;
        });
    if (resolved != shapeOffsetCache_.end()) {
        out.reserve(resolved->cells.size());
        for (const auto& offset : resolved->cells)
            out.push_back(geometry_.cellIndexAt(anchorRow + offset.dr, anchorCol + offset.dc,
                                                offset.base));
        return out;
    }

    // Small or malformed geometries may not have a usable central sample.
    // Preserve the exact geometric path as a defensive fallback.
    double ax = 0.0, ay = 0.0;
    geometry_.cellCenter(anchor, ax, ay);
    const bool anchorUp = geometry_.type != TilingType::Tri || (anchor % 2 == 0);
    out.reserve(shape->cells.size());
    for (const auto& cell : shape->cells) {
        out.push_back(geometry_.worldToCell(ax + cell.dx,
                                            ay + (anchorUp ? cell.dy : -cell.dy)));
    }
    return out;
}

void MapEditorModel::resolveCities() { rebuildResolution(); }

void MapEditorModel::rebuildResolution() {
    resolvedCities_.clear();
    resolvedCityIds_.assign(cells_.size(), -1);
    unresolvedMarks_.clear();
    warnings_.clear();

    std::vector<bool> seen(cells_.size(), false);
    const auto& set = cityConfig_.setFor(geometry_.type);
    std::vector<ResolvedComponent> nextCache;

    for (int start = 0; start < cellCount(); ++start) {
        if (!cityMarked(start) || seen[static_cast<std::size_t>(start)]) continue;
        const std::vector<int> component = markedComponent(start, seen);
        const auto cached = std::find_if(
            resolutionCache_.begin(), resolutionCache_.end(), [&](const ResolvedComponent& entry) {
                return entry.cells == component;
            });
        ResolvedComponent resolved;
        resolved.cells = component;
        if (cached != resolutionCache_.end()) {
            resolved.cities = cached->cities;
            resolved.cityCells = cached->cityCells;
            resolved.unresolved = cached->unresolved;
        } else {
        std::vector<bool> remaining(cells_.size(), false);
        for (int index : component) remaining[static_cast<std::size_t>(index)] = true;

        // Shape candidates are considered by highest configured level, then
        // variant and anchor index. This makes edits and exports byte-stable.
        std::vector<int> levelOrder(set.levels.size());
        for (std::size_t i = 0; i < set.levels.size(); ++i)
            levelOrder[i] = static_cast<int>(i);
        std::sort(levelOrder.begin(), levelOrder.end(), [&](int a, int b) {
            if (set.levels[static_cast<std::size_t>(a)] != set.levels[static_cast<std::size_t>(b)])
                return set.levels[static_cast<std::size_t>(a)] > set.levels[static_cast<std::size_t>(b)];
            return a < b;
        });

        while (true) {
            bool found = false;
            for (int li : levelOrder) {
                const double level = set.levels[static_cast<std::size_t>(li)];
                const int variants = set.variantCount(level);
                for (int variant = 0; variant < variants && !found; ++variant) {
                    const Config::City::Shape* shape = set.shapeFor(level, variant);
                    if (!shape) continue;
                    for (int anchor : component) {
                        const int baseCount = std::max(1, geometry_.baseCount());
                        const int base = anchor % baseCount;
                        if (shape->anchorBaseMask != 0 &&
                            (base >= 31 || (shape->anchorBaseMask & (1u << base)) == 0))
                            continue;
                        const std::vector<int> occupied = shapeCells(level, anchor, variant);
                        if (occupied.size() != shape->cells.size()) continue;
                        std::unordered_set<int> unique;
                        bool feasible = true;
                        for (int occupiedIndex : occupied) {
                            if (!validIndex(occupiedIndex) || !remaining[static_cast<std::size_t>(occupiedIndex)] ||
                                cells_[static_cast<std::size_t>(occupiedIndex)].terrain == MapTerrain::Sea ||
                                !unique.insert(occupiedIndex).second) {
                                feasible = false;
                                break;
                            }
                        }
                        if (!feasible) continue;
                        resolved.cities.push_back({level, anchor, variant});
                        resolved.cityCells.push_back(occupied);
                        for (int occupiedIndex : occupied) {
                            remaining[static_cast<std::size_t>(occupiedIndex)] = false;
                        }
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) break;
        }

        for (int index : component) {
            if (remaining[static_cast<std::size_t>(index)]) {
                resolved.unresolved.push_back(index);
            }
        }
        }
        const int cityBase = static_cast<int>(resolvedCities_.size());
        resolvedCities_.insert(resolvedCities_.end(), resolved.cities.begin(), resolved.cities.end());
        for (std::size_t i = 0; i < resolved.cityCells.size(); ++i) {
            const int cityId = cityBase + static_cast<int>(i);
            for (const int index : resolved.cityCells[i])
                resolvedCityIds_[static_cast<std::size_t>(index)] = cityId;
        }
        unresolvedMarks_.insert(unresolvedMarks_.end(), resolved.unresolved.begin(),
                                resolved.unresolved.end());
        if (!resolved.unresolved.empty())
            warnings_.push_back("unresolved city marks in edge-connected component: " +
                                std::to_string(resolved.unresolved.size()));
        nextCache.push_back(std::move(resolved));
    }
    resolutionCache_ = std::move(nextCache);
}

MapDefinition MapEditorModel::toDefinition() const {
    MapDefinition definition;
    definition.cols = geometry_.cols;
    definition.rows = geometry_.rows;
    definition.tiling = geometry_.type;
    definition.terrain.reserve(cells_.size());
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const MapEditorCell& cell = cells_[i];
        // City and mountain are land overlays. Keep the editor's visible
        // intent even when a city shape is unresolved or a mountain is coastal.
        definition.terrain.push_back(cell.terrain);
    }
    definition.cities = resolvedCities_;
    return definition;
}

bool MapEditorModel::saveToFile(const std::string& path, std::string* err) const {
    return toDefinition().saveToFile(path, err);
}

}  // namespace lw::editor
