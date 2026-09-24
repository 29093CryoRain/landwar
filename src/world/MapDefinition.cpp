#include "world/MapDefinition.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "world/tiling/Tiling.h"

namespace lw {

namespace {

using Json = nlohmann::json;

void setError(std::string* err, const std::string& message) {
    if (err) *err = message;
}

char terrainCode(MapTerrain terrain) {
    switch (terrain) {
        case MapTerrain::Sea: return 'S';
        case MapTerrain::Land: return 'L';
        case MapTerrain::Mountain: return 'M';
        case MapTerrain::City: return 'C';
    }
    return 'S';
}

bool parseTerrain(const Json& value, MapTerrain& out) {
    if (!value.is_string()) return false;
    const std::string name = value.get<std::string>();
    if (name == "sea") {
        out = MapTerrain::Sea;
        return true;
    }
    if (name == "land") {
        out = MapTerrain::Land;
        return true;
    }
    if (name == "mountain") {
        out = MapTerrain::Mountain;
        return true;
    }
    if (name == "city") {
        out = MapTerrain::City;
        return true;
    }
    return false;
}

bool lessEdgeRef(const MapEdgeRef& a, const MapEdgeRef& b) {
    return a.cell != b.cell ? a.cell < b.cell : a.edge < b.edge;
}

bool sameEdgeRef(const MapEdgeRef& a, const MapEdgeRef& b) {
    return a.cell == b.cell && a.edge == b.edge;
}

// 把 (cell,edge) 折到规范侧（同一条几何边两侧编码同一键）。无邻居/越界时原样返回。
MapEdgeRef canonicalEdgeRef(const TilingGeom& geometry, const MapEdgeRef& ref) {
    int cell = -1, edge = -1;
    const std::uint64_t key = geometry.edgeKey(ref.cell, ref.edge);
    if (key != 0 && geometry.edgeFromKey(key, cell, edge)) return {cell, edge};
    return ref;
}

}  // namespace

bool normalizeRiverRefs(const TilingGeom& geometry, const std::vector<MapEdgeRef>& input,
                        const std::function<bool(int)>& isLand, std::vector<MapEdgeRef>& out,
                        std::string* err) {
    out.clear();
    out.reserve(input.size());
    for (const MapEdgeRef& ref : input) {
        if (ref.cell < 0 || ref.cell >= geometry.cellCount() || ref.edge < 0 ||
            ref.edge >= geometry.neighborCount(ref.cell)) {
            setError(err, "river edge reference is out of range");
            return false;
        }
        const MapEdgeRef canonical = canonicalEdgeRef(geometry, ref);
        int a = -1, b = -1;
        geometry.edgeCells(canonical.cell, canonical.edge, a, b);
        if (b < 0) {
            setError(err, "river edge lies on the map boundary");
            return false;
        }
        if (!isLand(a) || !isLand(b)) {
            setError(err, "river edge touches sea");
            return false;
        }
        out.push_back(canonical);
    }
    std::sort(out.begin(), out.end(), lessEdgeRef);
    if (std::adjacent_find(out.begin(), out.end(), sameEdgeRef) != out.end()) {
        setError(err, "river edge appears more than once");
        return false;
    }
    return true;
}

bool MapDefinition::validate(std::string* err) const {
    if (cols <= 0 || rows <= 0) {
        setError(err, "map definition dimensions must be positive");
        return false;
    }
    if (static_cast<int>(tiling) < 0 || static_cast<int>(tiling) >= kTilingTypeCount) {
        setError(err, "map definition tiling is invalid");
        return false;
    }
    if (terrain.empty()) {
        setError(err, "map definition has no terrain cells");
        return false;
    }
    const TilingGeom geometry{tiling, cols, rows};
    if (terrain.size() != static_cast<size_t>(geometry.cellCount())) {
        setError(err, "map definition terrain count does not match geometry (" +
                       std::to_string(terrain.size()) + " vs " +
                       std::to_string(geometry.cellCount()) + ")");
        return false;
    }
    for (const MapTerrain value : terrain) {
        if (value != MapTerrain::Sea && value != MapTerrain::Land &&
            value != MapTerrain::Mountain && value != MapTerrain::City) {
            setError(err, "map definition has invalid terrain");
            return false;
        }
    }
    for (const auto& city : cities) {
        if (!std::isfinite(city.level) || city.level <= 0.0 || city.baseIndex < 0 ||
            city.baseIndex >= geometry.cellCount() ||
            city.shapeVariant < 0) {
            setError(err, "map definition has invalid city record");
            return false;
        }
    }
    std::vector<MapEdgeRef> normalizedRivers;
    if (!normalizeRiverRefs(
            geometry, rivers,
            [this](int index) {
                return terrain[static_cast<size_t>(index)] != MapTerrain::Sea;
            },
            normalizedRivers, err))
        return false;
    return true;
}

std::string MapDefinition::toJson() const {
    Json root;
    root["format"] = "landwar.map";
    root["version"] = 2;
    root["tiling"] = tilingName(tiling);
    root["cols"] = cols;
    root["rows"] = rows;
    const TilingGeom geometry{tiling, cols, rows};
    const int baseCount = std::max(1, geometry.baseCount());
    root["terrain"] = Json::array();
    for (int row = 0; row < rows; ++row) {
        std::string encoded;
        encoded.reserve(static_cast<size_t>(cols * baseCount));
        for (int col = 0; col < cols; ++col)
            for (int base = 0; base < baseCount; ++base)
                encoded.push_back(terrainCode(terrain[
                    (static_cast<size_t>(row) * cols + col) * baseCount + base]));
        root["terrain"].push_back(std::move(encoded));
    }
    root["cities"] = Json::array();
    for (const auto& city : cities) {
        root["cities"].push_back({{"level", city.level},
                                   {"baseIndex", city.baseIndex},
                                   {"shapeVariant", city.shapeVariant}});
    }
    // 河（§5.1）：始终输出（空数组也输出，便于人读与 diff）；规范 + 升序 + 去重，
    // 保证同一逻辑内容字节稳定。非法引用（临海/边界）由 validate() 拦截，不在此报错。
    std::vector<MapEdgeRef> ordered;
    ordered.reserve(rivers.size());
    for (const MapEdgeRef& ref : rivers) ordered.push_back(canonicalEdgeRef(geometry, ref));
    std::sort(ordered.begin(), ordered.end(), lessEdgeRef);
    ordered.erase(std::unique(ordered.begin(), ordered.end(), sameEdgeRef), ordered.end());
    root["rivers"] = Json::array();
    for (const MapEdgeRef& ref : ordered) root["rivers"].push_back({ref.cell, ref.edge});
    return root.dump(2);
}

bool MapDefinition::fromJson(const std::string& text, MapDefinition& out, std::string* err) {
    Json root;
    try {
        root = Json::parse(text, nullptr, true, true);
    } catch (const std::exception& e) {
        setError(err, std::string("map definition parse failed: ") + e.what());
        return false;
    }
    if (!root.is_object() || root.value("format", "") != "landwar.map" ||
        root.value("version", 0) != 2 || !root.contains("tiling") ||
        !root["tiling"].is_string() || !root.contains("cols") ||
        !root["cols"].is_number_integer() || !root.contains("rows") ||
        !root["rows"].is_number_integer() || !root.contains("terrain") ||
        !root["terrain"].is_array() || !root.contains("cities") ||
        !root["cities"].is_array()) {
        setError(err, "map definition has an invalid header");
        return false;
    }

    MapDefinition parsed;
    const std::string tiling = root["tiling"].get<std::string>();
    parsed.tiling = tilingFromName(tiling);
    if (tiling != tilingName(parsed.tiling)) {
        setError(err, "map definition has an unknown tiling");
        return false;
    }
    parsed.cols = root["cols"].get<int>();
    parsed.rows = root["rows"].get<int>();
    if (parsed.cols <= 0 || parsed.rows <= 0 || parsed.cols > 100000 || parsed.rows > 100000) {
        setError(err, "map definition dimensions are invalid");
        return false;
    }
    const Json& terrain = root["terrain"];
    const int baseCount = std::max(1, TilingGeom{parsed.tiling, parsed.cols, parsed.rows}.baseCount());
    const std::size_t rowWidth = static_cast<std::size_t>(parsed.cols * baseCount);
    const auto isCompactRow = [](const Json& value, std::size_t width) {
        if (!value.is_string() || value.get<std::string>().size() != width) return false;
        for (char code : value.get<std::string>())
             if (code != 'S' && code != 'L' && code != 'M' && code != 'C') return false;
        return true;
    };
    const bool rowEncoding = terrain.size() == static_cast<size_t>(parsed.rows) &&
                              !terrain.empty() && isCompactRow(terrain.front(), rowWidth);
    if (rowEncoding) {
        parsed.terrain.reserve(static_cast<size_t>(parsed.cols) * parsed.rows);
        for (const auto& row : terrain) {
            if (!isCompactRow(row, rowWidth)) {
                setError(err, "map definition has an invalid terrain row");
                return false;
            }
            for (char code : row.get<std::string>()) {
                if (code == 'S') parsed.terrain.push_back(MapTerrain::Sea);
                else if (code == 'L') parsed.terrain.push_back(MapTerrain::Land);
                else if (code == 'M') parsed.terrain.push_back(MapTerrain::Mountain);
                else if (code == 'C') parsed.terrain.push_back(MapTerrain::City);
                else {
                    setError(err, "map definition has an invalid terrain code");
                    return false;
                }
            }
        }
    } else {
        parsed.terrain.reserve(terrain.size());
        for (const auto& value : terrain) {
            MapTerrain parsedTerrain;
            if (!parseTerrain(value, parsedTerrain)) {
                setError(err, "map definition has an invalid terrain value");
                return false;
            }
            parsed.terrain.push_back(parsedTerrain);
        }
    }
    parsed.cities.reserve(root["cities"].size());
    for (const auto& value : root["cities"]) {
        if (!value.is_object() || !value.contains("level") || !value["level"].is_number() ||
            !value.contains("baseIndex") || !value["baseIndex"].is_number_integer() ||
            !value.contains("shapeVariant") || !value["shapeVariant"].is_number_integer()) {
            setError(err, "map definition has an invalid city record");
            return false;
        }
        parsed.cities.push_back({value["level"].get<double>(), value["baseIndex"].get<int>(),
                                 value["shapeVariant"].get<int>()});
    }
    // 河（§5.1）：缺键 = 无河；存在则逐项解析，合法性交给 validate()（越界/临海/重复）。
    if (root.contains("rivers")) {
        if (!root["rivers"].is_array()) {
            setError(err, "map definition rivers is not an array");
            return false;
        }
        parsed.rivers.reserve(root["rivers"].size());
        for (const auto& value : root["rivers"]) {
            if (!value.is_array() || value.size() != 2 || !value[0].is_number_integer() ||
                !value[1].is_number_integer()) {
                setError(err, "map definition has an invalid river record");
                return false;
            }
            parsed.rivers.push_back({value[0].get<int>(), value[1].get<int>()});
        }
    }
    if (!parsed.validate(err)) return false;
    out = std::move(parsed);
    return true;
}

bool MapDefinition::loadFromFile(const std::string& path, MapDefinition& out, std::string* err) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        setError(err, "map definition file not found: " + path);
        return false;
    }
    std::ostringstream text;
    text << ifs.rdbuf();
    return fromJson(text.str(), out, err);
}

bool MapDefinition::saveToFile(const std::string& path, std::string* err) const {
    if (!validate(err)) return false;
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        setError(err, "cannot open map definition for write: " + path);
        return false;
    }
    ofs << toJson();
    if (!ofs) {
        setError(err, "cannot write map definition: " + path);
        return false;
    }
    return true;
}

}  // namespace lw
