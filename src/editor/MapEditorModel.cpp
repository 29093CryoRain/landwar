#include "editor/MapEditorModel.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <unordered_set>

namespace lw::editor {

namespace {

MapEditorCell paintedCell(const MapEditorCell& current, MapTerrain terrain) {
    MapEditorCell next = current;
    switch (terrain) {
        case MapTerrain::Sea:
        case MapTerrain::Land:
            next.terrain = terrain;
            next.mountain = false;
            break;
        case MapTerrain::Mountain:
            next.mountain = true;
            if (next.terrain != MapTerrain::City) next.terrain = MapTerrain::Mountain;
            break;
        case MapTerrain::City:
            next.terrain = MapTerrain::City;
            break;
    }
    return next;
}

}  // namespace

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
    for (std::size_t i = 0; i < definition.terrain.size(); ++i) {
        cells_[i].terrain = definition.terrain[i];
        cells_[i].mountain = definition.terrain[i] == MapTerrain::Mountain;
    }

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

bool MapEditorModel::mountainMarked(int index) const {
    return validIndex(index) && cell(index).mountain;
}

int MapEditorModel::resolvedCityIdAt(int index) const {
    return validIndex(index) ? resolvedCityIds_[static_cast<std::size_t>(index)] : -1;
}

bool MapEditorModel::validIndex(int index) const { return index >= 0 && index < cellCount(); }

bool MapEditorModel::applyCell(int index, const MapEditorCell& next) {
    if (!validIndex(index)) return false;
    MapEditorCell& current = cells_[static_cast<std::size_t>(index)];
    if (current == next) return false;
    const State before = batchActive_ ? State{} : snapshot();
    const bool cityStateAffected = (current.terrain == MapTerrain::City) !=
                                   (next.terrain == MapTerrain::City);
    const bool addingCity = current.terrain != MapTerrain::City &&
                            next.terrain == MapTerrain::City;
    current = next;
    mountainCoastViolationsDirty_ = true;
    if (cityStateAffected) rebuildResolution({index});
    if (batchActive_) {
        batchChanged_ = true;
        if (addingCity) ++batchCityMarkIncrease_;
    } else {
        lastCityMarkIncrease_ = addingCity ? 1 : 0;
        beginOperation(before);
    }
    return true;
}

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
    rebuildResolution();
}

void MapEditorModel::beginOperation(const State& before) { undo_.push_back(before); }

void MapEditorModel::rebuildMountainCoastViolations() const {
    mountainCoastViolations_.clear();
    for (int index = 0; index < cellCount(); ++index) {
        if (!mountainMarked(index)) continue;
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
    if (!validIndex(index)) return false;
    return applyCell(index, paintedCell(cells_[static_cast<std::size_t>(index)], terrain));
}

bool MapEditorModel::setCityMark(int index, bool marked) {
    if (!validIndex(index) || cityMarked(index) == marked) return false;
    MapEditorCell next = cells_[static_cast<std::size_t>(index)];
    next.terrain = marked ? MapTerrain::City
                           : (next.mountain ? MapTerrain::Mountain : MapTerrain::Land);
    return applyCell(index, next);
}

int MapEditorModel::floodFillTerrain(int index, MapTerrain terrain) {
    if (!validIndex(index)) return 0;
    const MapTerrain original = terrainAt(index);
    if (original == terrain) return 0;
    const State before = batchActive_ ? State{} : snapshot();
    std::vector<int> todo{index};
    std::vector<bool> seen(cells_.size(), false);
    std::vector<int> changedIndices;
    bool cityStateAffected = false;
    int changed = 0;
    while (!todo.empty()) {
        const int current = todo.back();
        todo.pop_back();
        if (!validIndex(current) || seen[static_cast<std::size_t>(current)] ||
            terrainAt(current) != original)
            continue;
        seen[static_cast<std::size_t>(current)] = true;
        MapEditorCell& cell = cells_[static_cast<std::size_t>(current)];
        const MapEditorCell next = paintedCell(cell, terrain);
        const bool cityChanged = (cell.terrain == MapTerrain::City) !=
                                 (next.terrain == MapTerrain::City);
        if (cell != next) {
            cell = next;
            changedIndices.push_back(current);
            ++changed;
        }
        if (cityChanged) cityStateAffected = true;
        for (int k = 0; k < geometry_.neighborCount(current); ++k) {
            const int next = geometry_.neighbor(current, k);
            if (validIndex(next) && !seen[static_cast<std::size_t>(next)]) todo.push_back(next);
        }
    }
    if (changed > 0) {
        mountainCoastViolationsDirty_ = true;
        if (cityStateAffected) rebuildResolution(changedIndices);
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
    if (cityMarked(index) == marked) return 0;
    if (marked) return floodFillTerrain(index, MapTerrain::City);

    const MapTerrain original = terrainAt(index);
    const State before = batchActive_ ? State{} : snapshot();
    std::vector<int> todo{index};
    std::vector<bool> seen(cells_.size(), false);
    std::vector<int> changedIndices;
    int changed = 0;
    while (!todo.empty()) {
        const int current = todo.back();
        todo.pop_back();
        if (!validIndex(current) || seen[static_cast<std::size_t>(current)] ||
            terrainAt(current) != original)
            continue;
        seen[static_cast<std::size_t>(current)] = true;
        MapEditorCell& cell = cells_[static_cast<std::size_t>(current)];
        cell.terrain = cell.mountain ? MapTerrain::Mountain : MapTerrain::Land;
        changedIndices.push_back(current);
        ++changed;
        for (int k = 0; k < geometry_.neighborCount(current); ++k) {
            const int next = geometry_.neighbor(current, k);
            if (validIndex(next) && !seen[static_cast<std::size_t>(next)]) todo.push_back(next);
        }
    }
    if (changed == 0) return 0;
    mountainCoastViolationsDirty_ = true;
    rebuildResolution(changedIndices);
    if (batchActive_) {
        batchChanged_ = true;
    } else {
        lastCityMarkIncrease_ = 0;
        beginOperation(before);
    }
    return changed;
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

MapEditorModel::ResolvedComponent MapEditorModel::resolveComponent(
    const std::vector<int>& component) const {
    ResolvedComponent resolved;
    resolved.cells = component;
    const auto& set = cityConfig_.setFor(geometry_.type);

    // Keep the remaining set local to this component. The previous version
    // allocated a full-map bitmap for every component resolution.
    std::unordered_set<int> remaining;
    remaining.reserve(component.size());
    for (int index : component) remaining.insert(index);

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
                        if (!validIndex(occupiedIndex) ||
                            remaining.find(occupiedIndex) == remaining.end() ||
                            cells_[static_cast<std::size_t>(occupiedIndex)].terrain == MapTerrain::Sea ||
                            !unique.insert(occupiedIndex).second) {
                            feasible = false;
                            break;
                        }
                    }
                    if (!feasible) continue;
                    resolved.cities.push_back({level, anchor, variant});
                    resolved.cityCells.push_back(occupied);
                    for (int occupiedIndex : occupied) remaining.erase(occupiedIndex);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) break;
    }

    for (int index : component)
        if (remaining.find(index) != remaining.end()) resolved.unresolved.push_back(index);
    return resolved;
}

void MapEditorModel::rebuildResolvedViews() {
    std::sort(resolutionCache_.begin(), resolutionCache_.end(),
              [](const ResolvedComponent& a, const ResolvedComponent& b) {
                  return a.cells < b.cells;
              });
    resolvedCities_.clear();
    resolvedCityIds_.assign(cells_.size(), -1);
    unresolvedMarks_.clear();
    warnings_.clear();

    for (const auto& resolved : resolutionCache_) {
        const int cityBase = static_cast<int>(resolvedCities_.size());
        resolvedCities_.insert(resolvedCities_.end(), resolved.cities.begin(),
                               resolved.cities.end());
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
    }
}

void MapEditorModel::rebuildResolution() {
    resolutionCache_.clear();
    std::vector<bool> seen(cells_.size(), false);
    for (int start = 0; start < cellCount(); ++start) {
        if (!cityMarked(start) || seen[static_cast<std::size_t>(start)]) continue;
        resolutionCache_.push_back(resolveComponent(markedComponent(start, seen)));
    }
    rebuildResolvedViews();
}

void MapEditorModel::rebuildResolution(const std::vector<int>& changedIndices) {
    if (changedIndices.empty()) return;

    // A changed cell can merge or split a component, so include its immediate
    // neighbors when invalidating the old component and discovering the new one.
    std::vector<int> seeds;
    for (const int index : changedIndices) {
        if (!validIndex(index)) continue;
        seeds.push_back(index);
        for (int k = 0; k < geometry_.neighborCount(index); ++k) {
            const int neighbor = geometry_.neighbor(index, k);
            if (validIndex(neighbor)) seeds.push_back(neighbor);
        }
    }
    std::sort(seeds.begin(), seeds.end());
    seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());

    std::vector<bool> affected(resolutionCache_.size(), false);
    for (std::size_t component = 0; component < resolutionCache_.size(); ++component) {
        for (const int seed : seeds) {
            if (std::binary_search(resolutionCache_[component].cells.begin(),
                                  resolutionCache_[component].cells.end(), seed)) {
                affected[component] = true;
                break;
            }
        }
    }

    std::vector<ResolvedComponent> nextCache;
    nextCache.reserve(resolutionCache_.size() + seeds.size());
    for (std::size_t i = 0; i < resolutionCache_.size(); ++i)
        if (!affected[i]) nextCache.push_back(std::move(resolutionCache_[i]));

    std::vector<bool> seen(cells_.size(), false);
    for (const int seed : seeds) {
        if (!cityMarked(seed) || seen[static_cast<std::size_t>(seed)]) continue;
        nextCache.push_back(resolveComponent(markedComponent(seed, seen)));
    }
    resolutionCache_ = std::move(nextCache);
    rebuildResolvedViews();
}

MapDefinition MapEditorModel::toDefinition() const {
    MapDefinition definition;
    definition.cols = geometry_.cols;
    definition.rows = geometry_.rows;
    definition.tiling = geometry_.type;
    definition.terrain.reserve(cells_.size());
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const MapEditorCell& cell = cells_[i];
        // A resolved city can share its infrastructure cells with a mountain.
        // MapDefinition represents that combination as mountain terrain plus a
        // city record; unresolved marks remain city terrain for editor intent.
        if (cell.terrain == MapTerrain::City && cell.mountain && resolvedCityIds_[i] >= 0)
            definition.terrain.push_back(MapTerrain::Mountain);
        else
            definition.terrain.push_back(cell.terrain);
    }
    definition.cities = resolvedCities_;
    return definition;
}

bool MapEditorModel::saveToFile(const std::string& path, std::string* err) const {
    return toDefinition().saveToFile(path, err);
}

}  // namespace lw::editor
