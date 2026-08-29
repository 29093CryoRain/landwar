// MapGenerator.h — 随机地图生成（开发计划 P6 + P12 异种密铺）。
// C++ 实现：菜单内实时生成 → 落盘地形基图（方 = BMP，走现有 Map::loadFromBmp；
// 六/三角 = lwmap 自描述格式，走 Map::loadFromLwmap）→ 单一加载路径。
// 两段式结构：① 海拔场（value-noise fBm）→ ② 阈值切海陆 + 编码 R/G 概率通道。
// 确定性：(seed, params) 唯一决定生成的字节；生成器持独立 Rng(seed)，与局种子完全独立。
#pragma once

#include <cstdint>
#include <string>

#include "core/GameDefs.h"

namespace lw {

// 随机图参数（菜单「随机地图」页可编辑；options.json 持久化）。
struct MapGenParams {
    int width = 105;                // 随机图长（clamp 到 [32,200]；六 = 列数；三 = **视觉列数**，
                                    //   生成时列对数 = width/2 → 格数与方形一致，见 generate）
    int height = 95;                // 随机图宽（六/三 clamp 到偶数行）
    double seaRatio = 0.40;         // 海占比目标（clamp [0,0.9]，0 = 全陆地；0.9 = 陆地占比 0.1）
    double mountainDensity = 0.08;  // 内陆山占比目标（clamp [0,0.9]，期望值经骰子近似）
    double cityDensity = 0.02;      // 城占比目标（占陆地格）
    double cityMountainWeight = 0.3; // 山地格城市权重（普通陆地=1.0）
    bool forceCoast = false;        // 强制边缘为海：真实界外点邻接格必为海，并向内平滑削减海拔
    TilingType tiling = TilingType::Square;  // P12：密铺（square/hex/tri）
};

class MapGenerator {
public:
    // 生成地形基图到 path。返回 false 表示写盘失败（参数非法自动 clamp，不构成失败）。
    // 方 = BMP（与 Map::loadFromBmp 逐字节对齐：54 头 + 每行 width*3 像素 BGR + 1 字节填充）；
    // 六/三 = lwmap（magic "LWMP" + ver1 + tiling + cols + rows + 逐格 BGR 通道）。
    static bool generate(const std::string& path, std::uint32_t seed, const MapGenParams& p);

    // 默认生成路径：userdata/maps/gen_<seed>_<w>x<h>_<sea>_<mtn>_<city>.bmp（方）；
    // gen_<seed>_<tiling>_<w>x<h>_...lwmap（六/三）。自我描述、确定性。
    static std::string defaultPath(std::uint32_t seed, const MapGenParams& p);
};

}  // namespace lw
