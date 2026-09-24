#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/GameDefs.h"

namespace lw {

struct TilingGeom;  // world/tiling/Tiling.h

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

// 规范边引用（河流系统 §5.1）：cell = 规范键所属格，edge = 该格内与 neighbor/cellEdge
// 同序的边序号。文件格式**不使用内存打包键**（kMaxCellEdges 是实现细节，写进格式会
// 让"单格边数上限变化"变成不兼容变更）。
struct MapEdgeRef {
    int cell = -1;
    int edge = 0;

    bool operator==(const MapEdgeRef& other) const {
        return cell == other.cell && edge == other.edge;
    }
};

// 校验并规范化一组河边引用（河流系统 §5.1，R5 严格口径）：
//   越界格 / 越界边 / 基础域内无邻居 / 地图边界边 / 任一侧非陆地 / 重复 → 报错返回 false。
// 成功时 out = 规范 (cell,edge)（非规范输入会被折到规范侧）、按 (cell,edge) 升序、无重复。
// 文件里同时写了同一条边两侧 → 折到同一规范边后判定为重复（报错，不静默吞掉）。
// isLand 由调用方提供：地图定义用 terrain，运行时 Map 用 MapCell::land。
bool normalizeRiverRefs(const TilingGeom& geometry, const std::vector<MapEdgeRef>& input,
                        const std::function<bool(int)>& isLand, std::vector<MapEdgeRef>& out,
                        std::string* err = nullptr);

// Native, fully resolved map data. The cell arrays use the canonical tiling
// order (row-major, then base index for multi-base tilings).
struct MapDefinition {
    int cols = 0;
    int rows = 0;
    TilingType tiling = TilingType::Square;
    std::vector<MapTerrain> terrain;
    std::vector<MapCityDefinition> cities;
    // 河流系统 §5.1：可选字段（缺键 = 无河）；内存中恒为规范、升序、无重复。
    std::vector<MapEdgeRef> rivers;

    bool validate(std::string* err = nullptr) const;
    std::string toJson() const;
    static bool fromJson(const std::string& text, MapDefinition& out,
                         std::string* err = nullptr);
    static bool loadFromFile(const std::string& path, MapDefinition& out,
                             std::string* err = nullptr);
    bool saveToFile(const std::string& path, std::string* err = nullptr) const;
};

}  // namespace lw
