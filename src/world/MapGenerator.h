// MapGenerator.h — 随机地图生成（开发计划 P6，思路 3）。
// C++ 实现：菜单内实时生成 → 落盘地形基图 BMP → 走现有 Map::loadFromBmp 开局（单一加载路径）。
// 两段式结构：① 海拔场（value-noise fBm）→ ② 阈值切海陆 + 编码 R/G 概率通道。
// 确定性：(seed, params) 唯一决定生成的 BMP 字节；生成器持独立 Rng(seed)，与局种子（主种子）完全独立。
#pragma once

#include <cstdint>
#include <string>

namespace lw {

// 随机图参数（菜单「随机地图」页可编辑；options.json 持久化）。
struct MapGenParams {
    int width = 105;                // 随机图长（clamp 到 [32,200]）
    int height = 95;                // 随机图宽
    double seaRatio = 0.40;         // 海占比目标（clamp [0,0.9]，0 = 全陆地；0.9 = 陆地占比 0.1）
    double mountainDensity = 0.08;  // 内陆山占比目标（clamp [0,0.9]，期望值经骰子近似）
    double cityDensity = 0.02;      // 城占比目标（占陆地格）
    bool forceCoast = false;        // 强制边缘为海（2026-08-06）：边缘带按距离削减海拔高度，
                                    //   最外一圈恒为海 → 陆地占比 100% 时也只有恰好一圈海
};

class MapGenerator {
public:
    // 生成地形基图 BMP 到 path。返回 false 表示写盘失败（参数非法自动 clamp，不构成失败）。
    // BMP 编码与 Map::loadFromBmp 逐字节对齐：54 头 + 每行 width*3 像素 BGR + 1 字节填充
    // （width*3+1 行大小，非标准 4 对齐——仅游戏自产自读，文档注明）。
    static bool generate(const std::string& path, std::uint32_t seed, const MapGenParams& p);

    // 默认生成路径：userdata/maps/gen_<seed>_<w>x<h>_<sea>_<mtn>_<city>.bmp
    // （2026-08 工程改进：生成物入 userdata/；自我描述、确定性）。
    static std::string defaultPath(std::uint32_t seed, const MapGenParams& p);
};

}  // namespace lw
