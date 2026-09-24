// RiverGenerator.h — 随机成河（《河流系统开发文档》§9，R6）。
// 纯函数：只使用传入的 Rng（工程规范 §3 确定性红线），不碰文件系统/SDL；
// 输出规范 (cell,edge)、升序、去重，可直接写进 MapDefinition.rivers。
#pragma once

#include <cstdint>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "world/MapDefinition.h"
#include "world/tiling/Tiling.h"

namespace lw {

// 局部平面拟合的梯度向量（U 变化 / 坐标单位）。由 MapGenerator 的 estimateGradientVector
// 产生；随机成河只用它的方向（与顶点方向必须同帧，见 §9.6）。
struct GradVec {
    double x = 0.0;
    double y = 0.0;
};

// 生成河边集合。河数 = round(riverDensity × cellCount)；riverDensity <= 0 → 返回空且
// **不消耗任何 RNG**（无河随机图逐字节不变，§9.9）。
//   geometry        — 地图几何
//   land / mountain — 与 cellCount() 等长
//   gradVec         — 与 cellCount() 等长
//   params          — cfg.river.gen（权重/温度/终止上限）
//   gridCoordinates — true = gradVec 在**格坐标**帧（斜周期密铺）：顶点方向改用 gridVertex
std::vector<MapEdgeRef> generateRivers(const TilingGeom& geometry, const std::vector<bool>& land,
                                       const std::vector<bool>& mountain,
                                       const std::vector<GradVec>& gradVec, double riverDensity,
                                       const Config::River::Gen& params, bool gridCoordinates,
                                       Rng& rng);

}  // namespace lw
