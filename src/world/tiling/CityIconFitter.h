// CityIconFitter.h — 城市贴图缩放几何工具（半正/Laves 密铺阶段）。
// 纯几何、无 SDL、无 RNG。给定任意密铺下城市基建地块（若干凸多边形格子的并集，
// 世界坐标）与城市贴图基准宽高，计算轴对齐贴图矩形不超出基建地块范围且尽可能大
// 的缩放比例与放置中心（允许平移）。
#pragma once

#include <array>
#include <vector>

namespace lw {

// 世界坐标二维点。
using FitPoint = std::array<double, 2>;
// 一个凸多边形格子（逆时针或顺时针顶点序列均可，本工具不要求统一朝向）。
using FitPoly = std::vector<FitPoint>;

struct CityIconFitter {
    // 输入：
    //   cells  = 城市基建地块包含的全部格子（凸多边形，世界坐标）
    //   baseW / baseH = 贴图在"缩放 = 1"时的世界宽/高（通常 baseW=1、baseH=高/宽纵横比）
    // 输出：
    //   scale = 使贴图矩形不超出基建地块且尽可能大的缩放倍率（贴图宽 = baseW*scale）
    //   cx, cy = 贴图矩形中心的世界坐标（已允许平移）
    // 返回 false = 输入退化（格子为空/基准尺寸非正）。
    static bool compute(const std::vector<FitPoly>& cells, double baseW, double baseH,
                        double& scale, double& cx, double& cy);

    // 固定中心时，贴图不超出基建地块的最大缩放倍率（二分查找；单调）。公开供单测。
    static double maxScaleAt(const std::vector<FitPoly>& cells, double baseW, double baseH,
                             double cx, double cy);

    // 给定缩放倍率下，中心 (cx,cy) 处贴图矩形是否完全落在基建地块内（含边界）。
    // 公开供单测。基建地块须为"共享整边的凸多边形格子并集，且无洞"（城市形状满足）。
    static bool rectangleInside(const std::vector<FitPoly>& cells, double cx, double cy,
                                double baseW, double baseH, double scale);
};

}  // namespace lw
