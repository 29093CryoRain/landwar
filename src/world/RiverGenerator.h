// RiverGenerator.h — 随机成河（《河流系统开发文档》§9，R6）。
// 纯函数：只使用传入的 Rng（工程规范 §3 确定性红线），不碰文件系统/SDL；
// 输出规范 (cell,edge)、升序、去重，可直接写进 MapDefinition.rivers。
// 二期反馈：梯度幅度在 vertexGradient 里按 `params.gmin/gmax` 裁剪（smoothGradientMagnitude）；
// "更大采样面积"的梯度由 MapGenerator 用更大中心差分步长先算好再传入（gradVec 参数）。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "world/MapDefinition.h"
#include "world/tiling/Tiling.h"

namespace lw {

// 局部噪声场的梯度向量（U 变化 / 噪声坐标单位）。由 MapGenerator 从 ValueNoise2D 的中心差分
// 直接给出（与格型/格面积无关）；随机成河只用它的方向（与顶点方向必须同帧，见 §9.6）。
struct GradVec {
    double x = 0.0;
    double y = 0.0;
};

// 河流生成专用的梯度幅度裁剪（二期反馈）：g_smooth = clip(|g|, gmin, gmax)·g/|g|。
// |g| = 0（或非有限）→ 零向量（softmax 里该项退化为 0，不做除零）。
// **只在河流生成里生效**：山脉排序用的梯度仍是原始 |∇h|（见 MapGenerator）。
inline GradVec smoothGradientMagnitude(const GradVec& g, double gmin, double gmax) {
    const double mag = std::sqrt(g.x * g.x + g.y * g.y);
    if (!(mag > 0.0) || !std::isfinite(mag)) return {};
    const double clamped = std::clamp(mag, gmin, gmax);
    const double scale = clamped / mag;
    return {g.x * scale, g.y * scale};
}

// 生成统计（回归护栏用；nullptr = 不收集）。**核心不变量**：output.size() == traversals，
// 即每条边走且只走一次 —— 去重前后数目相同就等于"无重边"（见 .cpp 顶部的证明）。
struct RiverGenStats {
    int planned = 0;      // 计划尝试数 = round(河密度 × 陆地格数)
    int rivers = 0;       // 实际成河的条数（<= planned：源点无合法出边时会作废重试）
    int traversals = 0;   // 累计走过的边数（与去重后的边数必须相等）
    int sourceSkips = 0;  // 因"源顶点已在别的河上"而免费跳过的次数（不消耗尝试次数）
    int mountainSources = 0;  // 源自**山地**池的成功河流条数（<= rivers；观察山密度自适应用）
};

// 生成河边集合。**河流尝试数 = round(riverDensity × 陆地格数)**（§14 决策 D2）；
// riverDensity <= 0 → 返回空且**不消耗任何 RNG**（无河随机图逐字节不变，§9.9）。
//   geometry        — 地图几何
//   land / mountain — 与 cellCount() 等长
//   gradVec         — 与 cellCount() 等长。**河流专用**：MapGenerator 用
//                     `kGradientBaseStep × gen.gradientSmoothScale` 的更大中心差分步长算出
//                     （二期反馈），并在顶点处按 gen.gmin/gmax 裁剪幅度。
//   params          — cfg.river.gen（权重/源区权重/终止上限/梯度裁剪与平滑）
//   gridCoordinates — true = gradVec 在**格坐标**帧（斜周期密铺）：顶点方向改用 gridVertex
//   stats           — 可选统计输出（回归护栏用）
std::vector<MapEdgeRef> generateRivers(const TilingGeom& geometry, const std::vector<bool>& land,
                                       const std::vector<bool>& mountain,
                                       const std::vector<GradVec>& gradVec, double riverDensity,
                                       const Config::River::Gen& params, bool gridCoordinates,
                                       Rng& rng, RiverGenStats* stats = nullptr);

}  // namespace lw
