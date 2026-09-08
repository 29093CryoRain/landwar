// MapGenerator.h — 随机地图生成（开发计划 P6 + P12 异种密铺）。
// C++ 实现：菜单/无头模式直接生成 fully-resolved MapDefinition；文件输出是显式导出路径。
// 两段式结构：① 海拔场（value-noise fBm）→ ② 阈值切海陆 + 编码 R/G 概率通道。
// 确定性：(seed, params) 唯一决定生成的字节；生成器持独立 Rng(seed)，与局种子完全独立。
#pragma once

#include <cstdint>
#include <string>

#include "core/GameDefs.h"
#include "core/Config.h"
#include "world/MapDefinition.h"

namespace lw {

// 随机图参数（菜单「随机地图」页可编辑；options.json 持久化）。
struct MapGenParams {
    int width = 105;                // 随机图长（用户输入；canonical 周期块列数由映射得到）
    int height = 95;                // 随机图宽（用户输入；canonical 周期块行数由映射得到）
    double seaRatio = 0.40;         // 海占比目标（clamp [0,0.9]，0 = 全陆地；0.9 = 陆地占比 0.1）
    double mountainDensity = 0.08;  // 内陆山占比目标（clamp [0,0.9]，期望值经骰子近似）
    double cityDensity = 0.02;      // 城占比目标（占陆地格）
    double cityMountainWeight = 0.3; // 山地格城市权重（普通陆地=1.0）
    bool forceCoast = false;        // 强制边缘为海：真实界外点邻接格必为海，并向内平滑削减海拔
    TilingType tiling = TilingType::Square;  // P12：密铺（square/hex/tri）
    double forceCoastRangeMultiplier = 1.0;    // 边缘海拔衰减范围倍率
    double forceCoastStrengthMultiplier = 1.0; // 边缘海拔衰减强度倍率
};

class MapGenerator {
public:
    // Generate a resolved map in memory. No filesystem or RNG is needed by Map
    // when this definition is later loaded.
    static bool generate(std::uint32_t seed, const MapGenParams& params,
                         MapDefinition& out, const Config::City& cityConfig = Config::City{});

    // Explicit native export API. Runtime random-map initialization does not
    // use this overload.
    static bool generate(const std::string& path, std::uint32_t seed, const MapGenParams& p);

    // Default native export path, ending in .landmap.
    static std::string defaultPath(std::uint32_t seed, const MapGenParams& p);
};

}  // namespace lw
