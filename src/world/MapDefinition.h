#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/GameDefs.h"

namespace lw {

enum class MapTerrain : std::uint8_t {
    Sea,
    Land,
    Mountain,
    City,
};

struct MapCityDefinition {
    double level = 1.0;
    int baseIndex = -1;
    int shapeVariant = 0;
};

// Native, fully resolved map data. The cell arrays use the canonical tiling
// order (row-major, then base index for multi-base tilings).
struct MapDefinition {
    int cols = 0;
    int rows = 0;
    TilingType tiling = TilingType::Square;
    std::vector<MapTerrain> terrain;
    std::vector<MapCityDefinition> cities;

    bool validate(std::string* err = nullptr) const;
    std::string toJson() const;
    static bool fromJson(const std::string& text, MapDefinition& out,
                         std::string* err = nullptr);
    static bool loadFromFile(const std::string& path, MapDefinition& out,
                             std::string* err = nullptr);
    bool saveToFile(const std::string& path, std::string* err = nullptr) const;
};

}  // namespace lw
