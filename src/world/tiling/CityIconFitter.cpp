// CityIconFitter.cpp — 城市贴图缩放几何工具实现。
// 算法（小规模、精确贴合）：
//   1. 把"贴图矩形是否落在基建地块内"转化为三个可精确判定的条件：
//      a) 矩形四条边线段均被某几个凸格子的并集覆盖（逐边对每格做线段-凸多边形裁剪区间，
//         合并区间后覆盖 [0,1]）；
//      b) 基建地块并集的外边界边（只出现一次的格边）不与矩形内部相交；
//      c) 并集的外边界顶点不在矩形内部。
//      对无洞、共享整边的城市形状，a+b+c 等价于矩形 ⊆ 并集。
//   2. 固定中心时用二分查找最大缩放（单调）。
//   3. 平移搜索：候选中心（格心/外边界顶点/外边中点/区域包围盒网格/区域几何中心）
//      + 模式搜索局部精修（8 邻域坐标下降，步长逐半衰减）。
#include "world/tiling/CityIconFitter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lw {

namespace {

constexpr double kEps = 1e-9;
constexpr double kTol = 1e-7;  // 合并覆盖区间容差
constexpr double kEdgeTol2 = 1e-12;  // 边/顶点匹配容差（表驱动密铺顶点有 ~1e-7 舍入差）

double dist2(const FitPoint& a, const FitPoint& b) {
    const double dx = a[0] - b[0], dy = a[1] - b[1];
    return dx * dx + dy * dy;
}

// 点严格在矩形内部（不含边界；用于判定外边界元素是否戳进矩形内部）。
bool pointStrictlyInsideRect(double x, double y, double cx, double cy, double hw, double hh) {
    return x > cx - hw + kEps && x < cx + hw - kEps && y > cy - hh + kEps && y < cy + hh - kEps;
}

// 线段是否与矩形内部有正长度交集。
bool segmentHitsRectInterior(const FitPoint& a, const FitPoint& b, double cx, double cy,
                             double hw, double hh) {
    const double dx = b[0] - a[0], dy = b[1] - a[1];
    double t0 = 0.0, t1 = 1.0;
    // x ∈ (cx-hw, cx+hw)
    if (std::fabs(dx) < kEps) {
        if (!(a[0] > cx - hw + kEps && a[0] < cx + hw - kEps)) return false;
    } else {
        double lo = ((cx - hw) - a[0]) / dx;
        double hi = ((cx + hw) - a[0]) / dx;
        if (lo > hi) std::swap(lo, hi);
        t0 = std::max(t0, lo + kEps);  // 严格内部：开区间
        t1 = std::min(t1, hi - kEps);
    }
    // y ∈ (cy-hh, cy+hh)
    if (std::fabs(dy) < kEps) {
        if (!(a[1] > cy - hh + kEps && a[1] < cy + hh - kEps)) return false;
    } else {
        double lo = ((cy - hh) - a[1]) / dy;
        double hi = ((cy + hh) - a[1]) / dy;
        if (lo > hi) std::swap(lo, hi);
        t0 = std::max(t0, lo + kEps);
        t1 = std::min(t1, hi - kEps);
    }
    return t0 < t1;
}

// 点在凸多边形内（含边界；凸多边形顶点可顺/逆时针）。
bool pointInConvex(const FitPoly& poly, const FitPoint& p) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) return false;
    int sign = 0;
    for (int i = 0; i < n; ++i) {
        const FitPoint& a = poly[static_cast<size_t>(i)];
        const FitPoint& b = poly[static_cast<size_t>((i + 1) % n)];
        const double cross = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
        if (std::fabs(cross) < kTol) continue;
        const int s = cross > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (sign != s) return false;
    }
    return true;
}

bool pointInRegion(const std::vector<FitPoly>& cells, const FitPoint& p) {
    for (const auto& c : cells)
        if (pointInConvex(c, p)) return true;
    return false;
}

// 线段 AB 与凸多边形 cell 的交集参数区间 [t0,t1] ⊆ [0,1]；无交集返回 false。
// 凸多边形裁剪：对每条边内侧半平面截取。
bool segmentConvexInterval(const FitPoint& a, const FitPoint& b, const FitPoly& cell, double& t0,
                           double& t1) {
    const int n = static_cast<int>(cell.size());
    if (n < 3) return false;
    const double dx = b[0] - a[0], dy = b[1] - a[1];
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < n; ++i) {
        const FitPoint& p = cell[static_cast<size_t>(i)];
        const FitPoint& q = cell[static_cast<size_t>((i + 1) % n)];
        // 边向量 (ex,ey)；内侧法向取 (-ey, ex)（对逆时针）或 (ey, -ex)（对顺时针）。
        // 统一用点积符号决定半平面：内侧 = 使 cell 其余顶点 dot >= 0 的那一侧。
        const double ex = q[0] - p[0], ey = q[1] - p[1];
        double nx = -ey, ny = ex;
        // 找 cell 中第三个点判断法向朝内/外（用边终点相邻点即可；凸多边形取 (i+2)%n）。
        const FitPoint& r = cell[static_cast<size_t>((i + 2) % n)];
        if (nx * (r[0] - p[0]) + ny * (r[1] - p[1]) < 0) {
            nx = -nx;
            ny = -ny;
        }
        // 约束：dot(P(t)-p, n) >= 0
        const double denom = dx * nx + dy * ny;
        const double val = (a[0] - p[0]) * nx + (a[1] - p[1]) * ny;
        if (std::fabs(denom) < kEps) {
            if (val < -kTol) return false;  // 平行且在外侧
            continue;                       // 平行且在内侧：不裁
        }
        const double t = -val / denom;
        if (denom > 0) {
            lo = std::max(lo, t);  // 沿内法方向增大 → 下限
        } else {
            hi = std::min(hi, t);
        }
        if (lo > hi + kEps) return false;
    }
    t0 = std::max(0.0, lo);
    t1 = std::min(1.0, hi);
    return t0 <= t1 + kEps;
}

// 线段 AB 是否被 cells 的并集完全覆盖（逐格区间求并，检查 [0,1] 无洞）。
bool segmentCovered(const std::vector<FitPoly>& cells, const FitPoint& a, const FitPoint& b) {
    std::vector<std::pair<double, double>> iv;
    for (const auto& c : cells) {
        double t0, t1;
        if (!segmentConvexInterval(a, b, c, t0, t1)) continue;
        if (t1 < 0.0 - kEps || t0 > 1.0 + kEps) continue;
        iv.push_back({std::max(0.0, t0), std::min(1.0, t1)});
    }
    if (iv.empty()) return false;
    std::sort(iv.begin(), iv.end());
    double coverEnd = 0.0;
    for (const auto& [lo, hi] : iv) {
        if (lo > coverEnd + kTol) return false;  // 有洞
        coverEnd = std::max(coverEnd, hi);
        if (coverEnd >= 1.0 - kTol) return true;
    }
    return coverEnd >= 1.0 - kTol;
}

// 两条线段边是否同一段（顺序无关，容差）。
bool sameEdge(const FitPoint& a1, const FitPoint& b1, const FitPoint& a2, const FitPoint& b2) {
    const double d1 = dist2(a1, a2) + dist2(b1, b2);
    const double d2 = dist2(a1, b2) + dist2(b1, a2);
    return d1 < kEdgeTol2 || d2 < kEdgeTol2;
}

// 收集基建地块并集的外边界边（只出现一次的格边）与外边界顶点。
void buildBoundary(const std::vector<FitPoly>& cells, std::vector<std::pair<FitPoint, FitPoint>>& edges,
                   std::vector<FitPoint>& vertices) {
    struct RawEdge {
        FitPoint a, b;
        int count = 0;
    };
    std::vector<RawEdge> raw;
    for (const auto& c : cells) {
        const int n = static_cast<int>(c.size());
        for (int i = 0; i < n; ++i) {
            const FitPoint& a = c[static_cast<size_t>(i)];
            const FitPoint& b = c[static_cast<size_t>((i + 1) % n)];
            bool found = false;
            for (auto& re : raw) {
                if (sameEdge(re.a, re.b, a, b)) {
                    ++re.count;
                    found = true;
                    break;
                }
            }
            if (!found) raw.push_back({a, b, 1});
        }
    }
    edges.clear();
    vertices.clear();
    for (const auto& re : raw) {
        if (re.count != 1) continue;  // 出现两次 = 内边；出现两次以上 = 退化/共线，保留
        edges.push_back({re.a, re.b});
        vertices.push_back(re.a);
        vertices.push_back(re.b);
    }
    // 顶点去重。
    std::vector<FitPoint> uniq;
    for (const auto& v : vertices) {
        bool dup = false;
        for (const auto& u : uniq)
            if (dist2(u, v) < kEdgeTol2) {
                dup = true;
                break;
            }
        if (!dup) uniq.push_back(v);
    }
    vertices.swap(uniq);
}

void regionBounds(const std::vector<FitPoly>& cells, double& minX, double& minY, double& maxX,
                  double& maxY) {
    minX = minY = std::numeric_limits<double>::max();
    maxX = maxY = std::numeric_limits<double>::lowest();
    for (const auto& c : cells)
        for (const auto& p : c) {
            minX = std::min(minX, p[0]);
            minY = std::min(minY, p[1]);
            maxX = std::max(maxX, p[0]);
            maxY = std::max(maxY, p[1]);
        }
}

// 预处理后的区域（外边界缓存），避免每次 rectangleInside 重新 O(E²) 找边。
struct Region {
    std::vector<FitPoly> cells;
    std::vector<std::pair<FitPoint, FitPoint>> boundaryEdges;
    std::vector<FitPoint> boundaryVerts;
};

Region makeRegion(const std::vector<FitPoly>& cells) {
    Region r;
    r.cells = cells;
    buildBoundary(cells, r.boundaryEdges, r.boundaryVerts);
    return r;
}

bool rectangleInsideRegion(const Region& region, double cx, double cy, double baseW, double baseH,
                           double scale) {
    if (region.cells.empty() || baseW <= 0.0 || baseH <= 0.0 || scale <= 0.0) return false;
    const double hw = 0.5 * baseW * scale, hh = 0.5 * baseH * scale;
    const FitPoint corners[4] = {
        {cx - hw, cy - hh}, {cx + hw, cy - hh}, {cx + hw, cy + hh}, {cx - hw, cy + hh}};
    for (int i = 0; i < 4; ++i)
        if (!segmentCovered(region.cells, corners[i], corners[(i + 1) & 3])) return false;
    for (const auto& [a, b] : region.boundaryEdges)
        if (segmentHitsRectInterior(a, b, cx, cy, hw, hh)) return false;
    for (const auto& v : region.boundaryVerts)
        if (pointStrictlyInsideRect(v[0], v[1], cx, cy, hw, hh)) return false;
    return true;
}

double maxScaleAtRegion(const Region& region, double baseW, double baseH, double cx, double cy) {
    if (region.cells.empty() || baseW <= 0.0 || baseH <= 0.0) return 0.0;
    double minX, minY, maxX, maxY;
    regionBounds(region.cells, minX, minY, maxX, maxY);
    const double maxByBox = std::max(0.0, std::min((maxX - minX) / baseW, (maxY - minY) / baseH));
    double lo = 0.0, hi = maxByBox;
    if (!rectangleInsideRegion(region, cx, cy, baseW, baseH, hi)) {
        while (hi > 1e-9 && !rectangleInsideRegion(region, cx, cy, baseW, baseH, hi)) hi *= 0.5;
        if (hi <= 1e-9) return 0.0;
    }
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (rectangleInsideRegion(region, cx, cy, baseW, baseH, mid)) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

}  // namespace

bool CityIconFitter::rectangleInside(const std::vector<FitPoly>& cells, double cx, double cy,
                                     double baseW, double baseH, double scale) {
    return rectangleInsideRegion(makeRegion(cells), cx, cy, baseW, baseH, scale);
}

double CityIconFitter::maxScaleAt(const std::vector<FitPoly>& cells, double baseW, double baseH,
                                  double cx, double cy) {
    return maxScaleAtRegion(makeRegion(cells), baseW, baseH, cx, cy);
}

bool CityIconFitter::compute(const std::vector<FitPoly>& cells, double baseW, double baseH,
                             double& scale, double& cx, double& cy) {
    if (cells.empty() || baseW <= 0.0 || baseH <= 0.0) return false;
    const Region region = makeRegion(cells);
    double minX, minY, maxX, maxY;
    regionBounds(cells, minX, minY, maxX, maxY);

    std::vector<FitPoint> candidates;
    // 几何中心（各格中心平均）。
    FitPoint avg{0.0, 0.0};
    int nCell = 0;
    for (const auto& c : cells) {
        FitPoint cc{0.0, 0.0};
        for (const auto& p : c) {
            cc[0] += p[0];
            cc[1] += p[1];
        }
        const double s = static_cast<double>(c.size());
        cc[0] /= s;
        cc[1] /= s;
        avg[0] += cc[0];
        avg[1] += cc[1];
        ++nCell;
        candidates.push_back(cc);
    }
    if (nCell > 0) {
        avg[0] /= nCell;
        avg[1] /= nCell;
        candidates.push_back(avg);
    }
    // 外边界顶点与边中点。
    for (const auto& v : region.boundaryVerts) candidates.push_back(v);
    for (const auto& [a, b] : region.boundaryEdges)
        candidates.push_back({0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1])});
    // 包围盒网格（只取区域内的点）。粗网格先行，精修后足够逼近全局最优；
    // 外边界/格心候选保证"贴边"或"居格"的局部最优不被漏掉。
    const int kGrid = 32;
    for (int gy = 0; gy <= kGrid; ++gy) {
        const double y = minY + (maxY - minY) * static_cast<double>(gy) / kGrid;
        for (int gx = 0; gx <= kGrid; ++gx) {
            const double x = minX + (maxX - minX) * static_cast<double>(gx) / kGrid;
            if (pointInRegion(cells, {x, y})) candidates.push_back({x, y});
        }
    }

    scale = 0.0;
    cx = avg[0];
    cy = avg[1];
    // 平移搜索的更新准则：只有"显著更大"才替换；近似打平时偏好离几何中心更近的点，
    // 避免对称形状在数值噪声下漂到远离视觉中心的等价位置。
    const double centerScale = maxScaleAtRegion(region, baseW, baseH, avg[0], avg[1]);
    if (centerScale > 0.0) scale = centerScale;
    const auto accept = [&](double s, double px, double py) {
        const double rel = scale > 0.0 ? (s - scale) / scale : 1.0;
        if (rel > 1e-6 || (std::fabs(rel) <= 1e-6 && dist2({px, py}, {avg[0], avg[1]}) <
                                                           dist2({cx, cy}, {avg[0], avg[1]}))) {
            scale = s;
            cx = px;
            cy = py;
        }
    };
    for (const auto& c : candidates) {
        const double s = maxScaleAtRegion(region, baseW, baseH, c[0], c[1]);
        accept(s, c[0], c[1]);
    }

    // 模式搜索局部精修（8 邻域；步长逐半衰减）。步长从包围盒/32 起步，
    // 保证网格候选之间的空洞可被局部爬山覆盖。
    double step = std::max(maxX - minX, maxY - minY) / 32.0;
    while (step > 1e-8) {
        bool improved = false;
        for (int dy = -1; dy <= 1 && !improved; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const double nx = cx + dx * step, ny = cy + dy * step;
                if (nx < minX || nx > maxX || ny < minY || ny > maxY) continue;
                if (!pointInRegion(cells, {nx, ny})) continue;
                const double s = maxScaleAtRegion(region, baseW, baseH, nx, ny);
                if (s > scale * (1.0 + 1e-6)) {
                    scale = s;
                    cx = nx;
                    cy = ny;
                    improved = true;
                    break;
                }
            }
        }
        if (!improved) step *= 0.5;
    }
    // 中心缩放与最优几乎打平（<0.01%）→ 回中心（用户视觉中心优先；左右对称形状尤其重要）。
    if (centerScale > 0.0 && centerScale >= scale * (1.0 - 1e-6)) {
        scale = std::max(scale, centerScale);
        cx = avg[0];
        cy = avg[1];
    }
    return scale > 0.0;
}

}  // namespace lw
