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
    // 河密度（河流系统 §9.1）：河数 = round(riverDensity × **总格数**)；0 = 无河（不消耗 RNG）。
    // 成员默认 0.0（"不显式给值 = 无河"）；菜单/Options 侧默认 0.02。
    double riverDensity = 0.0;
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
    // riverGen = cfg.river.gen（随机成河参数；默认值即代码默认，测试可直接省略）。
    static bool generate(std::uint32_t seed, const MapGenParams& params, MapDefinition& out,
                         const Config::City& cityConfig = Config::City{},
                         const Config::River::Gen& riverGen = Config::River::Gen{});

    // Explicit native export API（导出到文件）。**必须转发与内存版相同的 city/river 配置**，
    // 否则同一个 (seed, params) 在"导出文件"与"直接生成"两条路径上会因等级幂律指数不同
    // 而给出不同的城市等级（2026-09-24 由 tests/test_mapgen 的往返用例暴露的既有隐患：
    // 该用例此前只因两次采样的等级恰好相同而偶然通过）。默认值只为兼容旧调用点。
    static bool generate(const std::string& path, std::uint32_t seed, const MapGenParams& p,
                         const Config::City& cityConfig = Config::City{},
                         const Config::River::Gen& riverGen = Config::River::Gen{});

    // Default native export path, ending in .landmap（含河密度，避免不同密度撞名）。
    static std::string defaultPath(std::uint32_t seed, const MapGenParams& p,
                                   const Config::River::Gen& riverGen = Config::River::Gen{});
};

}  // namespace lw
