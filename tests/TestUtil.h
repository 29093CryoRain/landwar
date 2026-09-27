// TestUtil.h — 单测共享工具。
#pragma once

#include <array>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "world/Map.h"
#include "world/MapDefinition.h"

namespace lwtest {

// 方格格坐标 (x, y) → 格下标 / 取格。通用地图 API 一律用格下标，这里只为方形测试省去手写乘加。
inline int cellIndex(const lw::Map& map, int x, int y) { return y * map.width() + x; }
inline lw::MapCell& atXY(lw::Map& map, int x, int y) { return map.atIndex(cellIndex(map, x, y)); }
inline const lw::MapCell& atXY(const lw::Map& map, int x, int y) {
    return map.atIndex(cellIndex(map, x, y));
}

// 可控 Rng：chance() 返回预设结果，便于确定性分支测试（Phase 3 下海/反弹、Phase 2 势力8 免费兵）。
class MockRng final : public lw::Rng {
public:
    std::vector<bool> results;
    std::size_t next = 0;
    std::vector<double> units;
    std::size_t nextUnit = 0;
    bool chance(double) override { return next < results.size() ? results[next++] : false; }
    double unit() override { return nextUnit < units.size() ? units[nextUnit++] : 0.5; }
};

inline lw::Config loadCfg() {
    return lw::Config::loadFromFile("data/config.jsonc");
}

// 测试生成的文件放到系统临时目录，不依赖构建目录名称（如 build/build-linux）。
inline std::string testArtifactPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("landwar_test_" + name)).string();
}

// 写一张 105×95 地形基图测试 BMP（P5 改版编码）：
//   默认普通陆 (128,128,128)；mountains 涂 (255,32,32)（r=255 → 确定性山）；
//   cityZones 涂 (32,200,32)（g=200 → 城概率≈0.57）；seas 涂 (0,0,0)。
// 与 C++ 读取器一致：54 头 + 标准 4 字节对齐行 + BGR + 自底向上。
// 用途：让地图相关测试不依赖 data/ 下随时可能被删除/改动的具体地图文件。
inline void writeTestMapLandmap(const std::string& path,
                                const std::vector<std::pair<int, int>>& mountains,
                                const std::vector<std::pair<int, int>>& cityZones,
                                const std::vector<std::pair<int, int>>& seas = {}) {
    constexpr int W = 105, H = 95;
    lw::MapDefinition definition;
    definition.cols = W;
    definition.rows = H;
    definition.terrain.assign(static_cast<std::size_t>(W * H), lw::MapTerrain::Land);
    for (const auto& [x, y] : mountains)
        definition.terrain[static_cast<std::size_t>(y * W + x)] = lw::MapTerrain::Mountain;
    for (const auto& [x, y] : seas)
        definition.terrain[static_cast<std::size_t>(y * W + x)] = lw::MapTerrain::Sea;
    for (const auto& [x, y] : cityZones)
        definition.terrain[static_cast<std::size_t>(y * W + x)] = lw::MapTerrain::City;
    for (const auto& [x, y] : cityZones)
        definition.cities.push_back({1.0, y * W + x, 0});
    std::string error;
    if (!definition.saveToFile(path, &error)) std::fputs(error.c_str(), stderr);
}

}  // namespace lwtest
