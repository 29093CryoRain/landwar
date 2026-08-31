// Tiling.cpp — 密铺几何实现（P12 异种地图）。纯几何，RNG 0 次。
// 坐标/邻接/穿越语义见 Tiling.h 头注与开发计划 P12 §1-§2。
#include "world/tiling/Tiling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace lw {

namespace {

// 射线 p + t·d 与线段 A→B 求交（2D 叉积法）。命中返回 t > kEps，否则 -1。
// 命中条件 t > kEps（起点贴边/顶点时跳过该边）且 u ∈ [-kEps, 1+kEps]（端点容差）。
double raySegHit(double px, double py, double dx, double dy, double ax, double ay, double bx,
                 double by) {
    const double ex = bx - ax, ey = by - ay;
    const double denom = dx * ey - dy * ex;
    if (std::fabs(denom) < kEps) return -1.0;  // 平行
    const double wx = ax - px, wy = ay - py;
    const double t = (wx * ey - wy * ex) / denom;
    const double u = (wx * dy - wy * dx) / denom;
    if (t > kEps && u >= -kEps && u <= 1.0 + kEps) return t;
    return -1.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 表驱动半正/Laves 密铺几何。
// 数据文件 data/tiling_specs_arch.json 由 tools/export_arch_specs.py 生成，
// 目前包含 5 种阿基米德密铺的基础域（3.3.4.3.4 与 3.3.3.3.6 待补）。
// 基础域 = 周期平行四边形 W=(wx,0)、H=(hx,hy)；B 个基础格在域内按格下标
//   idx = (r*cols + c)*B + b 平移复制：center = base[b].center + c*W + r*H。
// 邻接在首次加载时按"某基础格的边经 ±dr,±dc 平移后与另一基础格的边反向重合"求得。
// 5.1 的规则类型预建半区/象限候选，5.2 的复杂类型预建周期坐标细网格；两者均以
// 预计算半平面作为精确判定，边界不明确时保留原扫描兜底。网格短方向默认 128 档，
// 周期向量长度明显较长的方向使用两倍密度（实验记录见 .docs/Phase5性能实验记录.md）。
// ---------------------------------------------------------------------------
struct TilingTable {
    double wx = 0.0, wy = 0.0, hx = 0.0, hy = 0.0;  // W=(wx,wy)、H=(hx,hy)（一般平行四边形周期域）
    double rx = 0.0, ry = 0.0;            // 基础格多边形相对中心的 AABB 半径（保守）
    // 周期域真实世界范围：域内全部格顶点在轴对齐周期 [0,W)×[0,H) 上的最小/最大坐标。
    // 表驱动基元域的格可能**越出**周期盒（overhang，如 arch4.8.8 的斜方形+四角八边形），
    // 使 wx*cols / hy*rows 低估实际地图范围 → 边界穿越落出范围 → worldToCell=-1 → 卡死。
    // xmin/xmax/ymin/ymax 度量该 overhang，worldWidth/Height 据此返回真实范围。
    double xmin = 0.0, xmax = 0.0, ymin = 0.0, ymax = 0.0;
    struct Edge {
        int nb = -1;   // 邻格基础格下标
        int dr = 0;    // 邻格所在行偏移
        int dc = 0;    // 邻格所在列偏移
    };
    struct HalfPlane {
        // 格中心为原点的未归一化外法向方程：n·p <= d。
        double nx = 0.0;
        double ny = 0.0;
        double d = 0.0;
    };
    struct Cell {
        int n = 0;
        double cx = 0.0, cy = 0.0;
        std::vector<std::array<double, 2>> v;  // 逆时针顶点（与边序一致）
        std::vector<Edge> edges;               // 与 v[i]→v[i+1] 同序
        std::vector<HalfPlane> halfPlanes;     // 预计算的未归一化边界方程
    };
    struct FastCandidate {
        int b = -1;
        int dr = 0;
        int dc = 0;
    };
    std::vector<Cell> cells;
    // 表驱动点邻接：每项保存相对周期块和基础格，运行时再做有限地图边界检查。
    std::vector<std::vector<Edge>> vertexNeighbors;
    std::vector<double> cellAreas;
    std::vector<std::array<double, 2>> cellIncenters;
    // 5.1：周期块半区/象限内的候选 owner，边界仍回退精确扫描。
    int analyticRegionCount = 0;
    std::vector<std::vector<FastCandidate>> analyticCandidates;
    // 5.2：周期坐标细网格；b=-1 表示该小块跨越几何边界，必须回退。
    int lookupGridWidth = 0;
    int lookupGridHeight = 0;
    std::vector<FastCandidate> lookupGrid;
    // 地块双色分档（2026-08 异种地图开发思路「与双色渲染系统」）：loadTable 尾部派生。
    // paletteSize = 档数（0 = 不分档；方/六/三不计于此）；basePalette[b] = 每基础格档位。
    int paletteSize = 0;
    std::vector<int> basePalette;
};

namespace {

constexpr double kTableTol = 1e-6;

bool pointInHalfPlanes(const TilingTable::Cell& cell, double x, double y) {
    for (const auto& hp : cell.halfPlanes)
        if (hp.nx * x + hp.ny * y > hp.d + kTableTol) return false;
    return true;
}

bool pointInHalfPlanesStrict(const TilingTable::Cell& cell, double x, double y) {
    constexpr double kLookupMargin = 4.0 * kTableTol;
    for (const auto& hp : cell.halfPlanes)
        if (hp.nx * x + hp.ny * y >= hp.d - kLookupMargin) return false;
    return true;
}

bool isAnalyticType(TilingType t) {
    switch (t) {
        case TilingType::Arch33434:
        case TilingType::Arch3636:
        case TilingType::Arch488:
        case TilingType::Laves33434:
        case TilingType::Laves3464:
        case TilingType::Laves3636:
        case TilingType::Laves4612:
        case TilingType::Laves488:
        case TilingType::Laves31212: return true;
        default: return false;
    }
}

bool isGridType(TilingType t) {
    switch (t) {
        case TilingType::Arch33336:
        case TilingType::Arch31212:
        case TilingType::Arch3464:
        case TilingType::Arch4612:
        case TilingType::Laves33336: return true;
        default: return false;
    }
}

void periodicCoordinates(const TilingTable& tab, double x, double y, double& u, double& v) {
    const double det = tab.wx * tab.hy - tab.wy * tab.hx;
    u = (tab.hy * x - tab.hx * y) / det;
    v = (-tab.wy * x + tab.wx * y) / det;
}

void periodicPoint(const TilingTable& tab, double u, double v, double& x, double& y) {
    x = u * tab.wx + v * tab.hx;
    y = u * tab.wy + v * tab.hy;
}

void localVertex(const TilingTable& tab, const TilingTable::Cell& cell, int vertex, int dr, int dc,
                 double& u, double& v) {
    const auto& p = cell.v[static_cast<size_t>(vertex)];
    periodicCoordinates(tab, p[0], p[1], u, v);
    u += static_cast<double>(dc);
    v += static_cast<double>(dr);
}

bool inRect(double x, double y, double x0, double y0, double x1, double y1) {
    return x >= x0 - kTableTol && x <= x1 + kTableTol && y >= y0 - kTableTol &&
           y <= y1 + kTableTol;
}

double cross2(double ax, double ay, double bx, double by, double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool onSegment(double ax, double ay, double bx, double by, double px, double py) {
    return inRect(px, py, std::min(ax, bx), std::min(ay, by), std::max(ax, bx),
                  std::max(ay, by)) && std::fabs(cross2(ax, ay, bx, by, px, py)) <= kTableTol;
}

bool segmentsIntersect(double ax, double ay, double bx, double by, double cx, double cy, double dx,
                       double dy) {
    const double abC = cross2(ax, ay, bx, by, cx, cy);
    const double abD = cross2(ax, ay, bx, by, dx, dy);
    const double cdA = cross2(cx, cy, dx, dy, ax, ay);
    const double cdB = cross2(cx, cy, dx, dy, bx, by);
    if (((abC > kTableTol && abD < -kTableTol) || (abC < -kTableTol && abD > kTableTol)) &&
        ((cdA > kTableTol && cdB < -kTableTol) || (cdA < -kTableTol && cdB > kTableTol)))
        return true;
    return onSegment(ax, ay, bx, by, cx, cy) || onSegment(ax, ay, bx, by, dx, dy) ||
           onSegment(cx, cy, dx, dy, ax, ay) || onSegment(cx, cy, dx, dy, bx, by);
}

bool candidateIntersectsRect(const TilingTable& tab, const TilingTable::Cell& cell, int dr, int dc,
                             double x0, double y0, double x1, double y1) {
    std::array<std::array<double, 2>, 12> poly{};
    for (int i = 0; i < cell.n; ++i)
        localVertex(tab, cell, i, dr, dc, poly[static_cast<size_t>(i)][0],
                    poly[static_cast<size_t>(i)][1]);

    for (int i = 0; i < cell.n; ++i)
        if (inRect(poly[static_cast<size_t>(i)][0], poly[static_cast<size_t>(i)][1], x0, y0, x1,
                   y1))
            return true;

    const double corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const double ox = static_cast<double>(dc) * tab.wx + static_cast<double>(dr) * tab.hx;
    const double oy = static_cast<double>(dc) * tab.wy + static_cast<double>(dr) * tab.hy;
    for (const auto& corner : corners) {
        double px, py;
        periodicPoint(tab, corner[0], corner[1], px, py);
        if (pointInHalfPlanes(cell, px - (cell.cx + ox), py - (cell.cy + oy))) return true;
    }

    const double rectEdges[4][4] = {
        {x0, y0, x1, y0}, {x1, y0, x1, y1}, {x1, y1, x0, y1}, {x0, y1, x0, y0}};
    for (int i = 0; i < cell.n; ++i) {
        const auto& a = poly[static_cast<size_t>(i)];
        const auto& b = poly[static_cast<size_t>((i + 1) % cell.n)];
        for (const auto& edge : rectEdges)
            if (segmentsIntersect(a[0], a[1], b[0], b[1], edge[0], edge[1], edge[2], edge[3]))
                return true;
    }
    return false;
}

constexpr int kDefaultLookupGridTier = 128;

void buildFastIndexes(TilingTable& tab, TilingType t) {
    const int baseCount = static_cast<int>(tab.cells.size());
    if (baseCount <= 0) return;

    if (isAnalyticType(t)) {
        tab.analyticRegionCount = (t == TilingType::Arch3636) ? 2 : 4;
        tab.analyticCandidates.resize(static_cast<size_t>(tab.analyticRegionCount));
        for (int b = 0; b < baseCount; ++b) {
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    for (int region = 0; region < tab.analyticRegionCount; ++region) {
                        const double x0 = tab.analyticRegionCount == 2
                                              ? 0.0
                                              : ((region & 1) == 0 ? 0.0 : 0.5);
                        const double x1 = tab.analyticRegionCount == 2
                                              ? 1.0
                                              : ((region & 1) == 0 ? 0.5 : 1.0);
                        const double y0 = tab.analyticRegionCount == 2
                                              ? (region == 0 ? 0.0 : 0.5)
                                              : ((region & 2) == 0 ? 0.0 : 0.5);
                        const double y1 = tab.analyticRegionCount == 2
                                              ? (region == 0 ? 0.5 : 1.0)
                                              : ((region & 2) == 0 ? 0.5 : 1.0);
                        if (candidateIntersectsRect(tab, tab.cells[static_cast<size_t>(b)], dr,
                                                     dc, x0, y0, x1, y1))
                            tab.analyticCandidates[static_cast<size_t>(region)].push_back(
                                {b, dr, dc});
                    }
                }
            }
        }
        return;
    }

    if (!isGridType(t)) return;
    const int tier = kDefaultLookupGridTier;
    const double wLength = std::hypot(tab.wx, tab.wy);
    const double hLength = std::hypot(tab.hx, tab.hy);
    tab.lookupGridWidth = tier;
    tab.lookupGridHeight = tier;
    if (hLength > wLength * 1.25)
        tab.lookupGridHeight = tier * 2;
    else if (wLength > hLength * 1.25)
        tab.lookupGridWidth = tier * 2;
    const int gridWidth = tab.lookupGridWidth, gridHeight = tab.lookupGridHeight;
    tab.lookupGrid.assign(static_cast<size_t>(gridWidth * gridHeight), {});
    const double stepX = 1.0 / static_cast<double>(gridWidth);
    const double stepY = 1.0 / static_cast<double>(gridHeight);
    for (int gy = 0; gy < gridHeight; ++gy) {
        for (int gx = 0; gx < gridWidth; ++gx) {
            const double x0 = gx * stepX, x1 = (gx + 1) * stepX;
            const double y0 = gy * stepY, y1 = (gy + 1) * stepY;
            auto& result = tab.lookupGrid[static_cast<size_t>(gy * gridWidth + gx)];
            bool found = false;
            for (int b = 0; b < baseCount && !found; ++b) {
                const auto& cell = tab.cells[static_cast<size_t>(b)];
                for (int dr = -1; dr <= 1 && !found; ++dr) {
                    for (int dc = -1; dc <= 1 && !found; ++dc) {
                        const double ox = static_cast<double>(dc) * tab.wx + static_cast<double>(dr) * tab.hx;
                        const double oy = static_cast<double>(dc) * tab.wy + static_cast<double>(dr) * tab.hy;
                        const double corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
                        bool contained = true;
                        for (const auto& corner : corners) {
                            double px, py;
                            periodicPoint(tab, corner[0], corner[1], px, py);
                            if (!pointInHalfPlanesStrict(cell, px - (cell.cx + ox),
                                                          py - (cell.cy + oy))) {
                                contained = false;
                                break;
                            }
                        }
                        if (contained) {
                            result = {b, dr, dc};
                            found = true;
                        }
                    }
                }
            }
        }
    }
}

// 地块双色分档（定义见下；loadTable 尾部调用）。
void fillTilePalette(TilingTable& tab, TilingType t);

bool isTableType(TilingType t) {
    return static_cast<int>(t) > static_cast<int>(TilingType::Tri);
}

double dist2(const std::array<double, 2>& a, const std::array<double, 2>& b) {
    const double dx = a[0] - b[0], dy = a[1] - b[1];
    return dx * dx + dy * dy;
}

bool samePoint(const std::array<double, 2>& a, const std::array<double, 2>& b) {
    return dist2(a, b) <= kTableTol * kTableTol;
}

std::array<double, 2> polygonIncenter(const TilingTable::Cell& cell) {
    // 对每条边写出“到边的内侧距离 = 半径”，对三元组 (x,y,r) 做最小二乘。
    // Laves 基元来自浮点 JSON，最小二乘比单次边平分线求交更能吸收微小误差。
    double ata[3][3] = {};
    double atb[3] = {};
    double signedArea2 = 0.0;
    for (int i = 0; i < cell.n; ++i) {
        const auto& a = cell.v[static_cast<size_t>(i)];
        const auto& b = cell.v[static_cast<size_t>((i + 1) % cell.n)];
        signedArea2 += a[0] * b[1] - b[0] * a[1];
    }
    const double winding = signedArea2 >= 0.0 ? 1.0 : -1.0;
    for (int i = 0; i < cell.n; ++i) {
        const auto& a = cell.v[static_cast<size_t>(i)];
        const auto& b = cell.v[static_cast<size_t>((i + 1) % cell.n)];
        const double ex = b[0] - a[0], ey = b[1] - a[1];
        const double len = std::hypot(ex, ey);
        if (len <= kTableTol) continue;
        const double nx = winding * -ey / len;
        const double ny = winding * ex / len;
        const double row[3] = {nx, ny, -1.0};
        const double rhs = nx * a[0] + ny * a[1];
        for (int r = 0; r < 3; ++r) {
            atb[r] += row[r] * rhs;
            for (int c = 0; c < 3; ++c) ata[r][c] += row[r] * row[c];
        }
    }
    // 3x3 Gaussian elimination with partial pivoting.
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 3; ++row)
            if (std::fabs(ata[row][col]) > std::fabs(ata[pivot][col])) pivot = row;
        if (std::fabs(ata[pivot][col]) <= 1e-12) return {cell.cx, cell.cy};
        if (pivot != col) {
            for (int c = col; c < 3; ++c) std::swap(ata[col][c], ata[pivot][c]);
            std::swap(atb[col], atb[pivot]);
        }
        for (int row = col + 1; row < 3; ++row) {
            const double factor = ata[row][col] / ata[col][col];
            for (int c = col; c < 3; ++c) ata[row][c] -= factor * ata[col][c];
            atb[row] -= factor * atb[col];
        }
    }
    double solution[3] = {};
    for (int row = 2; row >= 0; --row) {
        double v = atb[row];
        for (int c = row + 1; c < 3; ++c) v -= ata[row][c] * solution[c];
        solution[row] = v / ata[row][row];
    }
    return {solution[0], solution[1]};
}

std::shared_ptr<const TilingTable> loadTable(TilingType t) {
    const char* name = tilingName(t);
    std::ifstream ifs("data/tiling_specs_arch.json");
    if (!ifs.is_open()) {
        spdlog::error("TilingTable: data/tiling_specs_arch.json not found");
        return nullptr;
    }
    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const std::exception& e) {
        spdlog::error("TilingTable: JSON parse failed: {}", e.what());
        return nullptr;
    }
    if (!root.contains(name)) {
        // Laves 数据在独立文件。
        ifs.close();
        ifs.open("data/tiling_specs_laves.json");
        if (!ifs.is_open()) {
            spdlog::warn("TilingTable: no spec for '{}'", name);
            return nullptr;
        }
        try {
            ifs >> root;
        } catch (const std::exception& e) {
            spdlog::error("TilingTable: laves JSON parse failed: {}", e.what());
            return nullptr;
        }
    }
    if (!root.contains(name)) {
        spdlog::warn("TilingTable: no spec for '{}'", name);
        return nullptr;
    }
    const auto& j = root[name];
    auto tab = std::make_shared<TilingTable>();
    tab->wx = j["W"][0].get<double>();
    tab->wy = (j["W"].size() > 1) ? j["W"][1].get<double>() : 0.0;  // 一般平行四边形周期 W.y（2026-08 斜周期）
    tab->hx = j["H"][0].get<double>();
    tab->hy = j["H"][1].get<double>();
    for (const auto& jc : j["cells"]) {
        TilingTable::Cell c;
        c.n = jc["n"].get<int>();
        c.cx = jc["cx"].get<double>();
        c.cy = jc["cy"].get<double>();
        for (const auto& jv : jc["v"])
            c.v.push_back({jv[0].get<double>(), jv[1].get<double>()});
        c.edges.resize(static_cast<size_t>(c.n));
        tab->cells.push_back(std::move(c));
    }
    // 预计算每个基础格的未归一化半平面。边界方程以格中心为原点，
    // 查询时只需对候选点做点积，避免临时多边形和逐次平移顶点。
    for (auto& cell : tab->cells) {
        double signedArea2 = 0.0;
        for (int i = 0; i < cell.n; ++i) {
            const auto& a = cell.v[static_cast<size_t>(i)];
            const auto& b = cell.v[static_cast<size_t>((i + 1) % cell.n)];
            signedArea2 += a[0] * b[1] - b[0] * a[1];
        }
        const double winding = signedArea2 >= 0.0 ? 1.0 : -1.0;
        cell.halfPlanes.reserve(static_cast<size_t>(cell.n));
        for (int i = 0; i < cell.n; ++i) {
            const auto& a = cell.v[static_cast<size_t>(i)];
            const auto& b = cell.v[static_cast<size_t>((i + 1) % cell.n)];
            const double ex = b[0] - a[0], ey = b[1] - a[1];
            const double nx = winding * ey;
            const double ny = -winding * ex;
            cell.halfPlanes.push_back({nx, ny,
                                       nx * (a[0] - cell.cx) + ny * (a[1] - cell.cy)});
        }
    }
    // 2026-08-23：方向规范已直接烘入数据（tools/gen_tiling_specs_v2.py 的 rot90：视角自旧
    // 中间朝向纠正 90°，W/H 互换）。产出的 axis-aligned 几何与旧运行时旋转后的产物逐点一致
    //（gen 脚本已交叉验证），故此处不再做运行时旋转。spec 现为轴对齐矩形周期域 W=(wx,0)、H=(0,hy)。
    // 保守 AABB 半径（相对格中心）+ 域真实范围（含 overhang）。
    for (const auto& c : tab->cells) {
        for (const auto& p : c.v) {
            tab->rx = std::max(tab->rx, std::fabs(p[0] - c.cx));
            tab->ry = std::max(tab->ry, std::fabs(p[1] - c.cy));
            tab->xmin = std::min(tab->xmin, p[0]);
            tab->xmax = std::max(tab->xmax, p[0]);
            tab->ymin = std::min(tab->ymin, p[1]);
            tab->ymax = std::max(tab->ymax, p[1]);
        }
    }
    // 面积和内切圆中心只依赖基础格，平移副本在查询时补上周期偏移。
    tab->cellAreas.resize(tab->cells.size(), 0.0);
    tab->cellIncenters.resize(tab->cells.size());
    for (size_t b = 0; b < tab->cells.size(); ++b) {
        const auto& cell = tab->cells[b];
        double area2 = 0.0;
        for (int i = 0; i < cell.n; ++i) {
            const auto& a = cell.v[static_cast<size_t>(i)];
            const auto& v = cell.v[static_cast<size_t>((i + 1) % cell.n)];
            area2 += a[0] * v[1] - v[0] * a[1];
        }
        tab->cellAreas[b] = std::fabs(area2) * 0.5;
        tab->cellIncenters[b] = polygonIncenter(cell);
    }
    // 建立邻接：每个基础格的每条边，找 ±dr,±dc 平移后与另一基础格反向重合的边。
    const int B = static_cast<int>(tab->cells.size());
    for (int b = 0; b < B; ++b) {
        TilingTable::Cell& cell = tab->cells[static_cast<size_t>(b)];
        for (int k = 0; k < cell.n; ++k) {
            const std::array<double, 2> A = cell.v[static_cast<size_t>(k)];
            const std::array<double, 2> Bv = cell.v[static_cast<size_t>((k + 1) % cell.n)];
            bool found = false;
            for (int dr = -2; dr <= 2 && !found; ++dr) {
                for (int dc = -2; dc <= 2 && !found; ++dc) {
                    const double ox = static_cast<double>(dc) * tab->wx + static_cast<double>(dr) * tab->hx;
                    const double oy = static_cast<double>(dc) * tab->wy + static_cast<double>(dr) * tab->hy;
                    for (int nb = 0; nb < B && !found; ++nb) {
                        const auto& ocell = tab->cells[static_cast<size_t>(nb)];
                        for (int kk = 0; kk < ocell.n; ++kk) {
                            std::array<double, 2> n0 = {ocell.v[static_cast<size_t>(kk)][0] + ox,
                                                         ocell.v[static_cast<size_t>(kk)][1] + oy};
                            std::array<double, 2> n1 = {ocell.v[static_cast<size_t>((kk + 1) % ocell.n)][0] + ox,
                                                         ocell.v[static_cast<size_t>((kk + 1) % ocell.n)][1] + oy};
                            if (samePoint(n0, Bv) && samePoint(n1, A)) {
                                cell.edges[static_cast<size_t>(k)] = {nb, dr, dc};
                                found = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (!found)
                spdlog::warn("TilingTable: no neighbor for {} base {} edge {}", name, b, k);
        }
    }
    // 顶点邻接：对每个基础格顶点，扫描附近周期块并按顶点重合建立去重表。
    // 平移范围 ±2 足以覆盖当前所有基础域；结果按 (dr,dc,base) 排序，保证查询稳定。
    tab->vertexNeighbors.resize(static_cast<size_t>(B));
    for (int b = 0; b < B; ++b) {
        const auto& cell = tab->cells[static_cast<size_t>(b)];
        auto& neighbors = tab->vertexNeighbors[static_cast<size_t>(b)];
        for (int dr = -2; dr <= 2; ++dr) {
            for (int dc = -2; dc <= 2; ++dc) {
                const double ox = static_cast<double>(dc) * tab->wx + static_cast<double>(dr) * tab->hx;
                const double oy = static_cast<double>(dc) * tab->wy + static_cast<double>(dr) * tab->hy;
                for (int nb = 0; nb < B; ++nb) {
                    if (nb == b && dr == 0 && dc == 0) continue;
                    const auto& other = tab->cells[static_cast<size_t>(nb)];
                    bool sharesVertex = false;
                    for (const auto& v : cell.v) {
                        for (const auto& ov : other.v) {
                            if (std::fabs(v[0] - (ov[0] + ox)) <= kTableTol &&
                                std::fabs(v[1] - (ov[1] + oy)) <= kTableTol) {
                                sharesVertex = true;
                                break;
                            }
                        }
                        if (sharesVertex) break;
                    }
                    if (!sharesVertex) continue;
                    const auto duplicate = std::find_if(
                        neighbors.begin(), neighbors.end(), [&](const TilingTable::Edge& e) {
                            return e.nb == nb && e.dr == dr && e.dc == dc;
                        });
                    if (duplicate == neighbors.end()) neighbors.push_back({nb, dr, dc});
                }
            }
        }
        std::sort(neighbors.begin(), neighbors.end(), [](const TilingTable::Edge& a,
                                                         const TilingTable::Edge& b) {
            if (a.dr != b.dr) return a.dr < b.dr;
            if (a.dc != b.dc) return a.dc < b.dc;
            return a.nb < b.nb;
        });
    }
    buildFastIndexes(*tab, t);
    fillTilePalette(*tab, t);
    return tab;
}

// ---- 地块双色分档（2026-08 异种地图开发思路「与双色渲染系统」）----
// 对表驱动密铺按 格类型/朝向 分档（方/六/三单色不分档，在此不处理）：
//   arch：按正多边形种类（边数）升序分档 → 档数 = 唯一边数 k；每格档 = 其边数排序序。
//   laves：统计全部格的朝向（旋转+镜像），按角度排序；以朝向种类数 G 的最小质因子 p 为
//          循环 → 档数 = p；每格档 = 朝向排序序 mod p。
// 纯几何、RNG 0；结果存 TilingTable（paletteSize/basePalette，非表驱动恒空）。
void fillTilePalette(TilingTable& tab, TilingType t) {
    const int B = static_cast<int>(tab.cells.size());
    if (B <= 0) {
        tab.paletteSize = 0;
        tab.basePalette.clear();
        return;
    }
    auto smallestPrimeFactor = [](int n) {
        if (n < 2) return 0;
        for (int p = 2; p * p <= n; ++p)
            if (n % p == 0) return p;
        return n;  // n 为素数
    };
    if (static_cast<int>(t) < static_cast<int>(TilingType::Laves3636)) {
        // arch：按正多边形种类（边数）升序分档。
        std::vector<int> sides;
        for (int b = 0; b < B; ++b) {
            const int n = tab.cells[static_cast<size_t>(b)].n;
            if (std::find(sides.begin(), sides.end(), n) == sides.end()) sides.push_back(n);
        }
        std::sort(sides.begin(), sides.end());
        tab.paletteSize = static_cast<int>(sides.size());
        tab.basePalette.resize(static_cast<size_t>(B));
        for (int b = 0; b < B; ++b) {
            const int n = tab.cells[static_cast<size_t>(b)].n;
            tab.basePalette[static_cast<size_t>(b)] =
                static_cast<int>(std::lower_bound(sides.begin(), sides.end(), n) - sides.begin());
        }
        return;
    }
    // laves：全部格朝向（旋转+镜像）分档。
    // 朝向签名 = (量化旋转角 θ, 手性 h)：θ = 相对格中心半径最大（并列取角度较大）顶点的
    // 极角；h = 该顶点与下一顶点的有向叉积符号（区分镜像——laves 单面正/反两种）。
    // 几何性质：同朝向格（仅平移）得同一签名；旋转 θ 随格转动；镜像仅翻转 h。经典互证。
    auto orientKey = [&](int b) -> std::pair<long, int> {
        const auto& c = tab.cells[static_cast<size_t>(b)];
        double bestR = -1.0, bestA = -1.0;
        int bestI = 0;
        for (int i = 0; i < c.n; ++i) {
            const double dx = c.v[static_cast<size_t>(i)][0] - c.cx;
            const double dy = c.v[static_cast<size_t>(i)][1] - c.cy;
            const double r = dx * dx + dy * dy;
            double a = std::atan2(dy, dx);
            if (a < 0.0) a += 2.0 * kPi;
            if (r > bestR + kTableTol || (std::fabs(r - bestR) <= kTableTol && a > bestA)) {
                bestR = r;
                bestA = a;
                bestI = i;
            }
        }
        const int ni = (bestI + 1) % c.n;
        const double dx0 = c.v[static_cast<size_t>(bestI)][0] - c.cx;
        const double dy0 = c.v[static_cast<size_t>(bestI)][1] - c.cy;
        const double dxn = c.v[static_cast<size_t>(ni)][0] - c.cx;
        const double dyn = c.v[static_cast<size_t>(ni)][1] - c.cy;
        const double cross = dx0 * dyn - dy0 * dxn;
        // 量化到 ~1e-3 rad（≈0.057°）：远大于浮点平移噪声（~1e-8）、远小于朝向角差
        //（laves 朝向角为格子常量，相差数十度）→ 同朝向聚并、异朝向不混。手性 h 区分镜像。
        const long q = std::lround(bestA * 1e3);
        const int h = cross >= 0.0 ? 1 : -1;
        return {q, h};
    };
    std::vector<std::pair<long, int>> keys(static_cast<size_t>(B));
    std::vector<std::pair<long, int>> distinct;
    for (int b = 0; b < B; ++b) {
        keys[static_cast<size_t>(b)] = orientKey(b);
        if (std::find(distinct.begin(), distinct.end(), keys[static_cast<size_t>(b)]) ==
            distinct.end())
            distinct.push_back(keys[static_cast<size_t>(b)]);
    }
    const int G = static_cast<int>(distinct.size());
    const int p = smallestPrimeFactor(G);  // 朝向种类数最小质因子（循环）
    tab.paletteSize = p;
    tab.basePalette.resize(static_cast<size_t>(B));
    if (p <= 1) {
        std::fill(tab.basePalette.begin(), tab.basePalette.end(), 0);  // 单色
        return;
    }
    std::sort(distinct.begin(), distinct.end());
    for (int b = 0; b < B; ++b) {
        auto it = std::lower_bound(distinct.begin(), distinct.end(), keys[static_cast<size_t>(b)]);
        const int rank = static_cast<int>(it - distinct.begin());
        tab.basePalette[static_cast<size_t>(b)] = rank % p;
    }
}

int tryFastTableCell(const TilingTable& tab, TilingType type, int cols, int rows, double x,
                     double y) {
    constexpr double kFastBoundaryTol = 1e-8;
    double u, v;
    periodicCoordinates(tab, x, y, u, v);
    const int col = static_cast<int>(std::floor(u));
    const int row = static_cast<int>(std::floor(v));
    const double fu = u - static_cast<double>(col);
    const double fv = v - static_cast<double>(row);
    if (fu <= kFastBoundaryTol || fu >= 1.0 - kFastBoundaryTol ||
        fv <= kFastBoundaryTol || fv >= 1.0 - kFastBoundaryTol)
        return -1;

    const auto tryCandidate = [&](const TilingTable::FastCandidate& candidate) {
        const int rr = row + candidate.dr, cc = col + candidate.dc;
        if (candidate.b < 0 || rr < 0 || rr >= rows || cc < 0 || cc >= cols) return -1;
        const auto& cell = tab.cells[static_cast<size_t>(candidate.b)];
        const double ox = static_cast<double>(cc) * tab.wx + static_cast<double>(rr) * tab.hx;
        const double oy = static_cast<double>(cc) * tab.wy + static_cast<double>(rr) * tab.hy;
        if (!pointInHalfPlanes(cell, x - (cell.cx + ox), y - (cell.cy + oy))) return -1;
        return (rr * cols + cc) * static_cast<int>(tab.cells.size()) + candidate.b;
    };

    if (isAnalyticType(type)) {
        int region = 0;
        if (tab.analyticRegionCount == 2) {
            if (std::fabs(fv - 0.5) <= kFastBoundaryTol) return -1;
            region = fv < 0.5 ? 0 : 1;
        } else {
            if (std::fabs(fu - 0.5) <= kFastBoundaryTol ||
                std::fabs(fv - 0.5) <= kFastBoundaryTol)
                return -1;
            region = (fu >= 0.5 ? 1 : 0) | (fv >= 0.5 ? 2 : 0);
        }
        for (const auto& candidate : tab.analyticCandidates[static_cast<size_t>(region)]) {
            const int result = tryCandidate(candidate);
            if (result >= 0) return result;
        }
        return -1;
    }

    if (tab.lookupGridWidth <= 0 || tab.lookupGridHeight <= 0 || tab.lookupGrid.empty()) return -1;
    const int gx = std::min(tab.lookupGridWidth - 1,
                            static_cast<int>(fu * static_cast<double>(tab.lookupGridWidth)));
    const int gy = std::min(tab.lookupGridHeight - 1,
                            static_cast<int>(fv * static_cast<double>(tab.lookupGridHeight)));
    return tryCandidate(tab.lookupGrid[static_cast<size_t>(gy * tab.lookupGridWidth + gx)]);
}

int scanTableCell(const TilingTable& tab, int cols, int rows, double worldX, double worldY,
                  double x, double y, double worldWidth, double worldHeight) {
    const int baseCount = static_cast<int>(tab.cells.size());
    const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
    const double xsh[4] = {0.0, cmax * tab.wx, rmax * tab.hx,
                           cmax * tab.wx + rmax * tab.hx};
    const double ysh[4] = {0.0, cmax * tab.wy, rmax * tab.hy,
                           cmax * tab.wy + rmax * tab.hy};
    const double wxlo = tab.xmin + *std::min_element(xsh, xsh + 4) - tab.rx;
    const double wxhi = tab.xmax + *std::max_element(xsh, xsh + 4) + tab.rx;
    const double wylo = tab.ymin + *std::min_element(ysh, ysh + 4) - tab.ry;
    const double wyhi = tab.ymax + *std::max_element(ysh, ysh + 4) + tab.ry;
    if (x < wxlo || x > wxhi || y < wylo || y > wyhi) return -1;

    int bestIdx = -1;
    double bestD2 = 1e18;
    const double det = tab.wx * tab.hy - tab.wy * tab.hx;
    for (int b = 0; b < baseCount; ++b) {
        const auto& cell = tab.cells[static_cast<size_t>(b)];
        const double px = x - cell.cx, py = y - cell.cy;
        const double c0f = (px * tab.hy - tab.hx * py) / det;
        const double r0f = (tab.wx * py - tab.wy * px) / det;
        const int c0 = static_cast<int>(std::lround(c0f));
        const int r0 = static_cast<int>(std::lround(r0f));
        for (int dr = -1; dr <= 1; ++dr) {
            const int r = r0 + dr;
            if (r < 0 || r >= rows) continue;
            for (int dc = -1; dc <= 1; ++dc) {
                const int c = c0 + dc;
                if (c < 0 || c >= cols) continue;
                const double ox = static_cast<double>(c) * tab.wx + static_cast<double>(r) * tab.hx;
                const double oy = static_cast<double>(c) * tab.wy + static_cast<double>(r) * tab.hy;
                if (pointInHalfPlanes(cell, x - (cell.cx + ox), y - (cell.cy + oy)))
                    return (r * cols + c) * baseCount + b;
                const double ccx = cell.cx + ox, ccy = cell.cy + oy;
                const double d2 = (x - ccx) * (x - ccx) + (y - ccy) * (y - ccy);
                if (d2 < bestD2) {
                    bestD2 = d2;
                    bestIdx = (r * cols + c) * baseCount + b;
                }
            }
        }
    }
    if (bestIdx >= 0 && bestD2 <= 4.0 * tab.rx * tab.rx && worldX >= -kTableTol &&
        worldX <= worldWidth + kTableTol && worldY >= -kTableTol &&
        worldY <= worldHeight + kTableTol)
        return bestIdx;
    return -1;
}

}  // namespace

void TilingGeom::ensureTable() const {
    if (!isTableType(type) || table_) return;
    table_ = loadTable(type);
}

int TilingGeom::tilePaletteSize() const {
    // 方/六/三：方、六 单色不分档；**三**有正/反两种朝向（同 laves 分档：朝向 2 种 → 最小
    // 质因子 2 循环 → 2 档），故单独返回 2（2026-08 用户定夺：三角正/反分别处理）。
    // 表驱动（arch/laves）：档数存表（arch = 唯一边数，laves = 朝向种类数最小质因子）。
    if (type == TilingType::Tri) return 2;
    if (!isTableType(type)) return 0;
    ensureTable();
    return table_ ? table_->paletteSize : 0;
}

int TilingGeom::tileColorIndex(int index) const {
    // 三角：朝向由格下标奇偶决定（偶 = 正/顶点朝上 = 0，奇 = 反/顶点朝下 = 1），与
    // cellCenter/cellPolygon 的 `(index & 1) == 0` 一致；档位 = 朝向排序序 mod 2 = index & 1。
    if (type == TilingType::Tri) return index & 1;
    if (!isTableType(type)) return 0;
    ensureTable();
    if (!table_ || table_->paletteSize <= 0) return 0;  // 不分档（单色）
    const int B = static_cast<int>(table_->cells.size());
    if (B <= 0 || index < 0 || index >= cellCount()) return 0;
    return table_->basePalette[static_cast<size_t>(index % B)];
}

int TilingGeom::cellCount() const {
    return baseCount() * cols * rows;
}

double TilingGeom::worldWidth() const {
    switch (type) {
        case TilingType::Square: return static_cast<double>(cols);
        case TilingType::Hex: return kHexColSpacing * static_cast<double>(cols);
        case TilingType::Tri: return kTriSide * static_cast<double>(cols);
        default: {
            ensureTable();
            // 真实世界轴对齐范围（含 overhang）：域在 (cols,rows) 展开后，格顶点
            // x = base.v.x + c*wx + r*hx，y = base.v.y + c*wy + r*hy。取四角 (c,r) ∈{0,cols-1}x{0,rows-1}
            // 的中心偏移极值 + 基础格 AABB 跨幅 = 真实 AABB。W.y/H.x 可为非零（平行四边形周期域）。
            if (!table_) return 0.0;
            const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
            const double xsh[4] = {0.0, cmax * table_->wx, rmax * table_->hx,
                                   cmax * table_->wx + rmax * table_->hx};
            const double xlo = *std::min_element(xsh, xsh + 4);
            const double xhi = *std::max_element(xsh, xsh + 4);
            return table_->xmax - table_->xmin + (xhi - xlo);
        }
    }
}

double TilingGeom::worldHeight() const {
    switch (type) {
        case TilingType::Square: return static_cast<double>(rows);
        case TilingType::Hex: return kHexRowSpacing * static_cast<double>(rows);
        case TilingType::Tri: return kTriAlt * static_cast<double>(rows);
        default: {
            ensureTable();
            if (!table_) return 0.0;
            const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
            const double ysh[4] = {0.0, cmax * table_->wy, rmax * table_->hy,
                                   cmax * table_->wy + rmax * table_->hy};
            const double ylo = *std::min_element(ysh, ysh + 4);
            const double yhi = *std::max_element(ysh, ysh + 4);
            return table_->ymax - table_->ymin + (yhi - ylo);
        }
    }
}

// 世界坐标域原点（AABB 下界）：格顶点 x = base.v.x + c*wx + r*hx。对表驱动密铺（含 overhang
// 与 W.y/H.x 剪切），四角 (c,r) ∈{0,cols-1}x{0,rows-1} 的偏移极值 + 基础格 AABB 下界 = 域左下角。
// cellCenter/cellPolygon 统一做 -worldMin 平移，把世界坐标归到 [0,worldWidth]x[0,worldHeight]。
// 这保证相机/兵特效/空间哈希的 [0,·] 夹取成立（2026-08：含 arch_33434 块整体左移后的负中心格）。
// 非表驱动（方/六/三）恒 0。
double TilingGeom::worldMinX() const {
    if (!isTableType(type)) return 0.0;
    ensureTable();
    if (!table_) return 0.0;
    const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
    const double xsh[4] = {0.0, cmax * table_->wx, rmax * table_->hx,
                           cmax * table_->wx + rmax * table_->hx};
    return table_->xmin + *std::min_element(xsh, xsh + 4);
}

double TilingGeom::worldMinY() const {
    if (!isTableType(type)) return 0.0;
    ensureTable();
    if (!table_) return 0.0;
    const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
    const double ysh[4] = {0.0, cmax * table_->wy, rmax * table_->hy,
                           cmax * table_->wy + rmax * table_->hy};
    return table_->ymin + *std::min_element(ysh, ysh + 4);
}

bool TilingGeom::hasSkewedPeriod() const {
    if (!isTableType(type)) return false;
    ensureTable();
    return table_ && (std::fabs(table_->wy) > kEps || std::fabs(table_->hx) > kEps);
}

int TilingGeom::neighborCount() const {
    switch (type) {
        case TilingType::Square: return 4;
        case TilingType::Hex: return 6;
        case TilingType::Tri: return 3;
        default: {
            ensureTable();
            // 表驱动类型各格边数不同；此处返回基础格中最大边数供旧式循环兜底。
            int m = 0;
            if (table_) for (const auto& c : table_->cells) m = std::max(m, c.n);
            return m;
        }
    }
}

int TilingGeom::neighborCount(int index) const {
    switch (type) {
        case TilingType::Square: return 4;
        case TilingType::Hex: return 6;
        case TilingType::Tri: return 3;
        default: {
            ensureTable();
            if (!table_ || index < 0 || index >= cellCount()) return 0;
            const int B = static_cast<int>(table_->cells.size());
            return table_->cells[static_cast<size_t>(index % B)].n;
        }
    }
}

int TilingGeom::baseCount() const {
    switch (type) {
        case TilingType::Square:
        case TilingType::Hex: return 1;
        case TilingType::Tri: return 2;
        default: {
            ensureTable();
            return table_ ? static_cast<int>(table_->cells.size()) : 0;
        }
    }
}

int TilingGeom::cellIndexAt(int r, int c, int b) const {
    const int B = baseCount();
    if (B <= 0 || r < 0 || r >= rows || c < 0 || c >= cols) return -1;
    if (B == 1) return (r * cols + c) * B;
    const int base = (type == TilingType::Tri) ? (b & 1) : b;
    if (base < 0 || base >= B) return -1;
    return (r * cols + c) * B + base;
}

void TilingGeom::indexToRowCol(int index, int& r, int& c, int& b) const {
    const int B = baseCount();
    if (B <= 0 || cols <= 0) {
        r = c = b = -1;
        return;
    }
    b = index % B;
    const int rc = index / B;
    r = rc / cols;
    c = rc % cols;
}

void TilingGeom::cellCenter(int index, double& wx, double& wy) const {
    switch (type) {
        case TilingType::Square: {
            wx = static_cast<double>(index % cols) + 0.5;
            wy = static_cast<double>(index / cols) + 0.5;
            return;
        }
        case TilingType::Hex: {
            const int r = index / cols, c = index % cols;
            wx = kHexColSpacing * (static_cast<double>(c) + 0.5 * (r & 1));
            wy = kHexRowSpacing * static_cast<double>(r) + 0.5 * kHexSide;
            return;
        }
        case TilingType::Tri: {
            // P12（2026-08 反馈）：三角形常规直边布局——所有行同列位（三角 j 的左角在
            // x = j·b/2，j ∈ [0,2N)），朝向随 (j + r) 奇偶翻转（行间无 b/2 平移）→
            // 左/右边缘贴 x=0 / x=N·b 的直边。正格左角 = b·i + p·b/2，反格左角 = b·i + (1-p)·b/2。
            const int pair = index >> 1;
            const int o = index & 1;
            const int r = pair / cols, i = pair % cols;
            const double p = static_cast<double>(r & 1);
            const double xLeft = kTriSide * (static_cast<double>(i) + (o == 0 ? 0.5 * p : 0.5 * (1.0 - p)));
            const double y0 = kTriAlt * static_cast<double>(r);
            wx = xLeft + 0.5 * kTriSide;
            wy = y0 + (o == 0 ? kTriAlt / 3.0 : 2.0 * kTriAlt / 3.0);
            return;
        }
        default: {
            ensureTable();
            if (!table_) break;
            const int B = static_cast<int>(table_->cells.size());
            const int b = index % B;
            const int rc = index / B;
            const int r = rc / cols;
            const int c = rc % cols;
            const auto& cell = table_->cells[static_cast<size_t>(b)];
            wx = cell.cx + static_cast<double>(c) * table_->wx + static_cast<double>(r) * table_->hx
                 - worldMinX();
            wy = cell.cy + static_cast<double>(c) * table_->wy + static_cast<double>(r) * table_->hy
                 - worldMinY();
            return;
        }
    }
    wx = wy = 0.0;
}

int TilingGeom::cellPolygon(int index, double* wx, double* wy, int maxVerts) const {
    double cx, cy;
    cellCenter(index, cx, cy);
    switch (type) {
        case TilingType::Square: {
            if (maxVerts < 4) return 0;
            wx[0] = cx - 0.5; wy[0] = cy - 0.5;
            wx[1] = cx + 0.5; wy[1] = cy - 0.5;
            wx[2] = cx + 0.5; wy[2] = cy + 0.5;
            wx[3] = cx - 0.5; wy[3] = cy + 0.5;
            return 4;
        }
        case TilingType::Hex: {
            if (maxVerts < 6) return 0;
            static const double kA[6] = {90.0, 30.0, -30.0, -90.0, -150.0, 150.0};
            for (int j = 0; j < 6; ++j) {
                const double rad = kA[j] * kPi / 180.0;
                wx[j] = cx + kHexSide * std::cos(rad);
                wy[j] = cy + kHexSide * std::sin(rad);
            }
            return 6;
        }
        case TilingType::Tri: {
            if (maxVerts < 3) return 0;
            const bool up = (index & 1) == 0;
            const double b = kTriSide, h = kTriAlt;
            if (up) {
                wx[0] = cx - b / 2.0; wy[0] = cy - h / 3.0;  // 底左
                wx[1] = cx + b / 2.0; wy[1] = cy - h / 3.0;  // 底右
                wx[2] = cx;           wy[2] = cy + 2.0 * h / 3.0;  // 顶
            } else {
                wx[0] = cx;           wy[0] = cy - 2.0 * h / 3.0;  // 顶（下尖）
                wx[1] = cx - b / 2.0; wy[1] = cy + h / 3.0;   // 底左
                wx[2] = cx + b / 2.0; wy[2] = cy + h / 3.0;   // 底右
            }
            return 3;
        }
        default: {
            ensureTable();
            if (!table_) return 0;
            const int B = static_cast<int>(table_->cells.size());
            const int b = index % B;
            const int rc = index / B;
            const int r = rc / cols;
            const int c = rc % cols;
            const auto& cell = table_->cells[static_cast<size_t>(b)];
            if (cell.n > maxVerts) return 0;
            const double ox = worldMinX(), oy = worldMinY();
            for (int i = 0; i < cell.n; ++i) {
                wx[i] = cell.v[static_cast<size_t>(i)][0] + static_cast<double>(c) * table_->wx
                        + static_cast<double>(r) * table_->hx - ox;
                wy[i] = cell.v[static_cast<size_t>(i)][1] + static_cast<double>(c) * table_->wy
                        + static_cast<double>(r) * table_->hy - oy;
            }
            return cell.n;
        }
    }
}

int TilingGeom::gridPolygon(int index, double* gx, double* gy, int maxVerts) const {
    // 非斜周期：格坐标 = 世界坐标（无剪切），直接走 cellPolygon。
    if (!hasSkewedPeriod()) return cellPolygon(index, gx, gy, maxVerts);
    // 斜周期：grid = R^{-1}(世界)（R = 格基 [W H]）。grid 下落点在 [0,cols]x[0,rows]、
    // 形状被剪切但仍填满矩形域 → "旋转到常规矩形"。R^{-1} = 1/det [[hy,-hx],[-wy,wx]]。
    ensureTable();
    if (!table_) return 0;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int r = rc / cols;
    const int c = rc % cols;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    if (cell.n > maxVerts) return 0;
    const double det = table_->wx * table_->hy - table_->wy * table_->hx;
    const double wx = table_->wx, wy = table_->wy, hx = table_->hx, hy = table_->hy;
    for (int i = 0; i < cell.n; ++i) {
        // 未平移世界顶点 = cell.v + c*W + r*H（-worldMin 已由 cellPolygon 抵消，这里用未平移帧）。
        const double ux = cell.v[static_cast<size_t>(i)][0] + static_cast<double>(c) * wx
                          + static_cast<double>(r) * hx;
        const double uy = cell.v[static_cast<size_t>(i)][1] + static_cast<double>(c) * wy
                          + static_cast<double>(r) * hy;
        gx[i] = (hy * ux - hx * uy) / det;
        gy[i] = (-wy * ux + wx * uy) / det;
    }
    return cell.n;
}

void TilingGeom::gridCenter(int index, double& gx, double& gy) const {
    if (!hasSkewedPeriod()) {
        cellCenter(index, gx, gy);
        return;
    }
    ensureTable();
    if (!table_) {
        gx = gy = 0.0;
        return;
    }
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int r = rc / cols;
    const int c = rc % cols;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    const double det = table_->wx * table_->hy - table_->wy * table_->hx;
    const double wx = table_->wx, wy = table_->wy, hx = table_->hx, hy = table_->hy;
    const double ux = cell.cx + static_cast<double>(c) * wx + static_cast<double>(r) * hx;
    const double uy = cell.cy + static_cast<double>(c) * wy + static_cast<double>(r) * hy;
    gx = (hy * ux - hx * uy) / det;
    gy = (-wy * ux + wx * uy) / det;
}

// 第 k 条邻接边的端点（与 neighbor/crossEdge 同序）：
//   六：邻接边 k（法向 k·60°）= 多边形顶点 (v[kE0[k]], v[kE1[k]])，kE0={1,0,5,4,3,2}、
//       kE1={2,1,0,5,4,3}（与 crossEdge 边表一致；顶点角 {90,30,-30,-90,-150,150}°）；
//   三正（v0=底左、v1=底右、v2=顶）：k0=底(v0,v1)、k1=左斜(v0,v2)、k2=右斜(v2,v1)；
//   三反（v0=下尖、v1=底左、v2=底右）：k0=顶(v1,v2)、k1=右斜(v2,v0)、k2=左斜(v0,v1)。
bool TilingGeom::cellEdge(int index, int k, double& x0, double& y0, double& x1, double& y1) const {
    double vx[12], vy[12];
    const int n = cellPolygon(index, vx, vy, 12);
    if (n < 3 || k < 0 || k >= n) return false;
    if (type == TilingType::Square) {
        // 邻接顺序保持历史语义：0 下、1 上、2 左、3 右；几何顶点仍按逆时针存储。
        static const int kE0[4] = {0, 3, 0, 1};
        static const int kE1[4] = {1, 2, 3, 2};
        if (k >= 0 && k < 4) {
            x0 = vx[kE0[k]]; y0 = vy[kE0[k]];
            x1 = vx[kE1[k]]; y1 = vy[kE1[k]];
            return true;
        }
        return false;
    }
    if (type == TilingType::Hex) {
        static const int kE0[6] = {1, 0, 5, 4, 3, 2};
        static const int kE1[6] = {2, 1, 0, 5, 4, 3};
        x0 = vx[kE0[k]]; y0 = vy[kE0[k]];
        x1 = vx[kE1[k]]; y1 = vy[kE1[k]];
        return true;
    }
    if (type == TilingType::Tri) {
        const bool up = (index & 1) == 0;
        if (k == 0) {  // 正：底边 (v0,v1)；反：顶边 (v1,v2)
            x0 = vx[up ? 0 : 1]; y0 = vy[up ? 0 : 1];
            x1 = vx[up ? 1 : 2]; y1 = vy[up ? 1 : 2];
            return true;
        }
        if (k == 1) {  // 正：左斜 (v0,v2)；反：右斜 (v2,v0)
            x0 = vx[up ? 0 : 2]; y0 = vy[up ? 0 : 2];
            x1 = vx[up ? 2 : 0]; y1 = vy[up ? 2 : 0];
            return true;
        }
        // k == 2：正：右斜 (v2,v1)；反：左斜 (v0,v1)
        x0 = vx[up ? 2 : 0]; y0 = vy[up ? 2 : 0];
        x1 = vx[up ? 1 : 1]; y1 = vy[up ? 1 : 1];
        return true;
    }
    // 表驱动：边 k 与多边形顶点序一致。
    x0 = vx[k]; y0 = vy[k];
    x1 = vx[(k + 1) % n]; y1 = vy[(k + 1) % n];
    return true;
}

int TilingGeom::worldToCell(double wx, double wy) const {
    switch (type) {
        case TilingType::Square: {
            const int x = static_cast<int>(std::floor(wx));
            const int y = static_cast<int>(std::floor(wy));
            if (x < 0 || x >= cols || y < 0 || y >= rows) return -1;
            return y * cols + x;
        }
        case TilingType::Hex: {
            // 候选行/列（round 最近），再 3×3 窗口验证包含（含右缘凸出列——奇数行右顶点
            // 贴 worldWidth、偶数行仅至 (N-1/2)·√3a，候选列可能 = cols 越界 → 扫描窗口内有效格）。
            const int r = static_cast<int>(std::lround((wy - 0.5 * kHexSide) / kHexRowSpacing));
            // 包含判定：|dy| ≤ a 且 |dx| ≤ min(√3a/2, √3(a-|dy|))。
            const auto contains2 = [&](int rr, int cc) {
                if (rr < 0 || rr >= rows || cc < 0 || cc >= cols) return false;
                double cx, cy;
                cellCenter(rr * cols + cc, cx, cy);
                const double dy = wy - cy, dx = wx - cx;
                const double ay = std::fabs(dy);
                const double lim = std::min(0.5 * kHexColSpacing,
                                            kHexColSpacing * (1.0 - ay / kHexSide));
                return ay <= kHexSide + kEps && std::fabs(dx) <= lim + kEps;
            };
            // 确定性扫描序：先候选行 r，列候选 c 优先，再 ±1（贴边点归属候选格，与旧行为一致）。
            for (int dr = -1; dr <= 1; ++dr) {
                const int rr = r + dr;
                if (rr < 0 || rr >= rows) continue;
                const int c = static_cast<int>(std::lround(wx / kHexColSpacing - 0.5 * (rr & 1)));
                for (int dc : {0, -1, 1}) {
                    const int cc = c + dc;
                    if (cc < 0 || cc >= cols) continue;
                    if (contains2(rr, cc)) return rr * cols + cc;
                }
            }
            return -1;
        }
        case TilingType::Tri: {
            const double h = kTriAlt, b = kTriSide;
            int r = static_cast<int>(std::floor(wy / h));
            if (r < 0) return -1;
            if (r >= rows) {
                if (wy <= worldHeight() + kEps)
                    r = rows - 1;
                else
                    return -1;
            }
            // P12 直边布局（行内无 b/2 平移）：p = r 奇偶。
            //   正格(i)：底左角 x = b·i + p·b/2，范围 [底左 + t·b/2, 底左 + b − t·b/2]（t：自底向上）；
            //   反格(i)：顶角 x = b·i + (1-p)·b/2 + b/2，范围 [顶 ± t·b/2]（t：自顶向下）。
            const double p = static_cast<double>(r & 1);
            const double t = (wy - h * static_cast<double>(r)) / h;
            const int i0 = static_cast<int>(std::floor(wx / b));
            // 规范检查序（确定性）：up(i0), down(i0), up(i0-1), down(i0-1), up(i0+1), down(i0+1)。
            const auto inUp = [&](int i) {
                if (i < 0 || i >= cols) return false;
                const double xl = b * (static_cast<double>(i) + 0.5 * p);  // 底左角 x
                return wx >= xl + t * b / 2.0 - kEps && wx <= xl + b - t * b / 2.0 + kEps;
            };
            const auto inDown = [&](int i) {
                if (i < 0 || i >= cols) return false;
                const double ax = b * (static_cast<double>(i) + 0.5 * (1.0 - p)) + 0.5 * b;  // 顶角 x
                return wx >= ax - t * b / 2.0 - kEps && wx <= ax + t * b / 2.0 + kEps;
            };
            const int cand[6] = {i0, i0, i0 - 1, i0 - 1, i0 + 1, i0 + 1};
            for (int k = 0; k < 6; ++k) {
                const int i = cand[k];
                const bool up = (k % 2 == 0);
                if ((up && inUp(i)) || (!up && inDown(i)))
                    return 2 * (r * cols + i) + (up ? 0 : 1);
            }
            return -1;
        }
        default: {
            ensureTable();
            if (!table_) return -1;
            const double ux = wx + worldMinX(), uy = wy + worldMinY();
            const int fast = tryFastTableCell(*table_, type, cols, rows, ux, uy);
            if (fast >= 0) return fast;
            return scanTableCell(*table_, cols, rows, wx, wy, ux, uy, worldWidth(), worldHeight());
        }
    }
    return -1;
}

void TilingGeom::rowRange(double y0, double y1, int& r0, int& r1) const {
    switch (type) {
        case TilingType::Square:
            r0 = static_cast<int>(std::floor(y0));
            r1 = static_cast<int>(std::floor(y1));
            break;
        case TilingType::Hex:
            r0 = static_cast<int>(std::lround((y0 - 0.5 * kHexSide) / kHexRowSpacing)) - 1;
            r1 = static_cast<int>(std::lround((y1 - 0.5 * kHexSide) / kHexRowSpacing)) + 1;
            break;
        case TilingType::Tri:
            r0 = static_cast<int>(std::floor(y0 / kTriAlt)) - 1;
            r1 = static_cast<int>(std::floor(y1 / kTriAlt)) + 1;
            break;
        default:
            ensureTable();
            if (table_) {
                // 2026-08 斜周期：W.y≠0 时各列中心 y 随 c 平移（剪切），行不再与 y 一一对应——
                // 保守返回全行区间（正确但可能松散）；H.x 剪切由每行 colRange 的 baseX=r*hx 处理。
                if (std::fabs(table_->wy) > kEps || std::fabs(table_->hy) < kEps) {
                    r0 = 0;
                    r1 = rows - 1;
                } else {
                    r0 = static_cast<int>(std::floor((y0 - table_->ry) / table_->hy)) - 1;
                    r1 = static_cast<int>(std::ceil((y1 + table_->ry) / table_->hy)) + 1;
                }
            } else {
                r0 = 0;
                r1 = rows - 1;
            }
            break;
    }
    r0 = std::clamp(r0, 0, std::max(0, rows - 1));
    r1 = std::clamp(r1, 0, std::max(0, rows - 1));
}

void TilingGeom::colRange(double x0, double x1, int r, int& c0, int& c1) const {
    switch (type) {
        case TilingType::Square:
            c0 = static_cast<int>(std::floor(x0));
            c1 = static_cast<int>(std::floor(x1));
            break;
        case TilingType::Hex: {
            const double p = 0.5 * static_cast<double>(r & 1);
            c0 = static_cast<int>(std::lround(x0 / kHexColSpacing - p)) - 1;
            c1 = static_cast<int>(std::lround(x1 / kHexColSpacing - p)) + 1;
            break;
        }
        case TilingType::Tri: {
            // P12 直边布局：所有行同列位（三角 j 左角 x = j·b/2），无奇偶偏移。
            c0 = static_cast<int>(std::floor(x0 / kTriSide)) - 1;
            c1 = static_cast<int>(std::floor(x1 / kTriSide)) + 1;
            break;
        }
        default:
            ensureTable();
            if (table_) {
                // 世界 x 已整体平移（-worldMinX）→ 加回后用未平移帧求解列区间。
                const double ox = worldMinX();
                const double baseX = static_cast<double>(r) * table_->hx;
                c0 = static_cast<int>(std::floor((x0 + ox - baseX - table_->rx) / table_->wx)) - 1;
                c1 = static_cast<int>(std::ceil((x1 + ox - baseX + table_->rx) / table_->wx)) + 1;
            } else {
                c0 = 0;
                c1 = cols - 1;
            }
            break;
    }
    c0 = std::clamp(c0, 0, std::max(0, cols - 1));
    c1 = std::clamp(c1, 0, std::max(0, cols - 1));
}

void TilingGeom::neighborRaw(int index, int k, int& r, int& c) const {
    switch (type) {
        case TilingType::Square: {
            const int x = index % cols, y = index / cols;
            switch (k) {
                case 0: r = y + 1; c = x; return;  // 下
                case 1: r = y - 1; c = x; return;  // 上
                case 2: r = y; c = x - 1; return;  // 左
                default: r = y; c = x + 1; return; // 右
            }
        }
        case TilingType::Hex: {
            const int rr = index / cols, cc = index % cols;
            const int p = rr & 1;
            switch (k) {
                case 0: r = rr; c = cc + 1; return;          // 右
                case 1: r = rr + 1; c = cc + p; return;      // 右上
                case 2: r = rr + 1; c = cc - 1 + p; return;  // 左上
                case 3: r = rr; c = cc - 1; return;          // 左
                case 4: r = rr - 1; c = cc - 1 + p; return;  // 左下
                default: r = rr - 1; c = cc + p; return;     // 右下
            }
        }
        case TilingType::Tri: {
            // P12 直边布局邻接（p = rr 奇偶；三角邻格恒为反向）：
            //   正 k0 底 → 反(i, r-1)（同列下邻）；k1 左斜 → 反(i+p-1, r)；k2 右斜 → 反(i+p, r)。
            //   反 k0 顶 → 正(i, r+1)（同列上邻）；k1 右斜 → 正(i+1-p, r)；k2 左斜 → 正(i-p, r)。
            const int pair = index >> 1;
            const int o = index & 1;
            const int rr = pair / cols, i = pair % cols;
            const int p = rr & 1;
            if (o == 0) {  // 正：0 底、1 左斜、2 右斜
                switch (k) {
                    case 0: r = rr - 1; c = i; return;
                    case 1: r = rr; c = i + p - 1; return;
                    default: r = rr; c = i + p; return;
                }
            } else {  // 反：0 顶、1 右斜、2 左斜
                switch (k) {
                    case 0: r = rr + 1; c = i; return;
                    case 1: r = rr; c = i + 1 - p; return;
                    default: r = rr; c = i - p; return;
                }
            }
        }
        default: {
            ensureTable();
            if (!table_) break;
            const int B = static_cast<int>(table_->cells.size());
            const int b = index % B;
            const int rc = index / B;
            const int rr = rc / cols;
            const int cc = rc % cols;
            const auto& cell = table_->cells[static_cast<size_t>(b)];
            if (k < 0 || k >= cell.n) break;
            const auto& e = cell.edges[static_cast<size_t>(k)];
            r = rr + e.dr;
            c = cc + e.dc;
            return;
        }
    }
    r = c = -1;
}

int TilingGeom::neighbor(int index, int k) const {
    if (index < 0 || k < 0 || k >= neighborCount(index)) return -1;
    if (type == TilingType::Tri) {
        const int pair = index >> 1;
        const int o = index & 1;
        const int rr = pair / cols, i = pair % cols;
        const int p = rr & 1;
        int tr, ti;
        if (o == 0) {
            switch (k) {
                case 0: tr = rr - 1; ti = i; break;
                case 1: tr = rr; ti = i + p - 1; break;
                default: tr = rr; ti = i + p; break;
            }
        } else {
            switch (k) {
                case 0: tr = rr + 1; ti = i; break;
                case 1: tr = rr; ti = i + 1 - p; break;
                default: tr = rr; ti = i - p; break;
            }
        }
        if (tr < 0 || tr >= rows || ti < 0 || ti >= cols) return -1;
        return 2 * (tr * cols + ti) + (o == 0 ? 1 : 0);  // 三角邻格恒为反向
    }
    int r, c;
    neighborRaw(index, k, r, c);
    if (r < 0 || r >= rows || c < 0 || c >= cols) return -1;
    if (isTableType(type)) {
        ensureTable();
        if (!table_) return -1;
        const int B = static_cast<int>(table_->cells.size());
        const int b = index % B;
        const auto& cell = table_->cells[static_cast<size_t>(b)];
        if (k < 0 || k >= cell.n) return -1;
        return (r * cols + c) * B + cell.edges[static_cast<size_t>(k)].nb;
    }
    return r * cols + c;
}

int TilingGeom::pointNeighborCount(int index) const {
    if (index < 0 || index >= cellCount()) return 0;
    switch (type) {
        case TilingType::Square: return 8;
        case TilingType::Hex: return 6;
        case TilingType::Tri: return 12;
        default:
            ensureTable();
            if (!table_ || table_->cells.empty()) return 0;
            return static_cast<int>(table_->vertexNeighbors[static_cast<size_t>(index % table_->cells.size())].size());
    }
}

int TilingGeom::pointNeighbor(int index, int k) const {
    if (index < 0 || index >= cellCount() || k < 0 || k >= pointNeighborCount(index)) return -1;
    if (type == TilingType::Square) {
        const int x = index % cols, y = index / cols;
        static constexpr int kDx[8] = {0, 0, -1, 1, -1, 1, -1, 1};
        static constexpr int kDy[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        const int nx = x + kDx[k], ny = y + kDy[k];
        return nx >= 0 && nx < cols && ny >= 0 && ny < rows ? ny * cols + nx : -1;
    }
    if (type == TilingType::Hex) return neighbor(index, k);
    if (type == TilingType::Tri) {
        // A triangle only meets cells in its own row or an adjacent row. Enumerate
        // that fixed neighbourhood, then compare vertices to handle both orientations.
        const int pair = index >> 1;
        const int row = pair / cols, col = pair % cols;
        double vx[3], vy[3];
        const int vn = cellPolygon(index, vx, vy, 3);
        std::array<int, 32> matches{};
        int matchCount = 0;
        for (int rr = row - 1; rr <= row + 1; ++rr) {
            for (int cc = col - 2; cc <= col + 2; ++cc) {
                for (int b = 0; b < 2; ++b) {
                    const int candidate = cellIndexAt(rr, cc, b);
                    if (candidate < 0 || candidate == index) continue;
                    double cvx[3], cvy[3];
                    const int cn = cellPolygon(candidate, cvx, cvy, 3);
                    bool shares = false;
                    for (int i = 0; i < vn && !shares; ++i)
                        for (int j = 0; j < cn; ++j)
                            if (std::fabs(vx[i] - cvx[j]) <= kTableTol &&
                                std::fabs(vy[i] - cvy[j]) <= kTableTol) {
                                shares = true;
                                break;
                            }
                    if (shares && matchCount < static_cast<int>(matches.size()))
                        matches[static_cast<size_t>(matchCount++)] = candidate;
                }
            }
        }
        std::sort(matches.begin(), matches.begin() + matchCount);
        const auto end = std::unique(matches.begin(), matches.begin() + matchCount);
        matchCount = static_cast<int>(end - matches.begin());
        return k < matchCount ? matches[static_cast<size_t>(k)] : -1;
    }
    ensureTable();
    if (!table_ || table_->cells.empty()) return -1;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int row = rc / cols, col = rc % cols;
    const auto& candidates = table_->vertexNeighbors[static_cast<size_t>(b)];
    if (k >= static_cast<int>(candidates.size())) return -1;
    const auto& edge = candidates[static_cast<size_t>(k)];
    const int nr = row + edge.dr, nc = col + edge.dc;
    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) return -1;
    return (nr * cols + nc) * B + edge.nb;
}

double TilingGeom::cellArea(int index) const {
    if (index < 0 || index >= cellCount()) return 0.0;
    if (!isTableType(type)) return 1.0;
    ensureTable();
    if (!table_ || table_->cells.empty()) return 0.0;
    return table_->cellAreas[static_cast<size_t>(index % table_->cells.size())];
}

void TilingGeom::cellIncenter(int index, double& wx, double& wy) const {
    if (index < 0 || index >= cellCount()) {
        wx = wy = 0.0;
        return;
    }
    if (!isTableType(type)) {
        cellCenter(index, wx, wy);
        return;
    }
    ensureTable();
    if (!table_ || table_->cells.empty()) {
        wx = wy = 0.0;
        return;
    }
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int row = rc / cols, col = rc % cols;
    const auto& center = table_->cellIncenters[static_cast<size_t>(b)];
    wx = center[0] + static_cast<double>(col) * table_->wx + static_cast<double>(row) * table_->hx
         - worldMinX();
    wy = center[1] + static_cast<double>(col) * table_->wy + static_cast<double>(row) * table_->hy
         - worldMinY();
}

int TilingGeom::crossEdge(int index, double& x, double& y, double angle, double& remLength) const {
    if (index < 0) return -1;
    const double dx = std::cos(angle), dy = std::sin(angle);
    const double cx = x, cy = y;
    double minT = 1e18;
    double minU = 0.0;
    int bestK = -1;
    const auto testEdge = [&](double ax, double ay, double bx, double by, int k) {
        const double t = raySegHit(cx, cy, dx, dy, ax, ay, bx, by);
        if (t > 0.0 && t < minT) {
            minT = t;
            bestK = k;
            // 反解 u（命中点在边上的参数；端点 = 顶点命中）
            const double ex = bx - ax, ey = by - ay;
            const double denom = dx * ey - dy * ex;
            minU = ((ax - cx) * dy - (ay - cy) * dx) / denom;
        }
    };

    switch (type) {
        case TilingType::Square: {
            for (int k = 0; k < 4; ++k) {
                double ax, ay, bx, by;
                if (cellEdge(index, k, ax, ay, bx, by)) testEdge(ax, ay, bx, by, k);
            }
            break;
        }
        case TilingType::Hex: {
            double hx, hy;
            cellCenter(index, hx, hy);
            // 顶点角（度）：90,30,-30,-90,-150,150 → 边 k = (顶点 (k+1)%6, (k+2)%6)。
            static const double kA[6] = {90.0, 30.0, -30.0, -90.0, -150.0, 150.0};
            static const int kE0[6] = {1, 0, 5, 4, 3, 2};
            static const int kE1[6] = {2, 1, 0, 5, 4, 3};
            double vx[6], vy[6];
            for (int j = 0; j < 6; ++j) {
                const double rad = kA[j] * kPi / 180.0;
                vx[j] = hx + kHexSide * std::cos(rad);
                vy[j] = hy + kHexSide * std::sin(rad);
            }
            for (int k = 0; k < 6; ++k)
                testEdge(vx[kE0[k]], vy[kE0[k]], vx[kE1[k]], vy[kE1[k]], k);
            break;
        }
        case TilingType::Tri: {
            double tx, ty;
            cellCenter(index, tx, ty);
            const bool up = (index & 1) == 0;
            const double b = kTriSide, h = kTriAlt;
            double vx[3], vy[3];
            if (up) {
                vx[0] = tx - b / 2.0; vy[0] = ty - h / 3.0;   // 底左
                vx[1] = tx + b / 2.0; vy[1] = ty - h / 3.0;   // 底右
                vx[2] = tx;           vy[2] = ty + 2.0 * h / 3.0;  // 顶
                testEdge(vx[0], vy[0], vx[1], vy[1], 0);       // 底
                testEdge(vx[0], vy[0], vx[2], vy[2], 1);       // 左斜
                testEdge(vx[2], vy[2], vx[1], vy[1], 2);       // 右斜
            } else {
                vx[0] = tx;           vy[0] = ty - 2.0 * h / 3.0;  // 顶（下尖）
                vx[1] = tx - b / 2.0; vy[1] = ty + h / 3.0;    // 底左
                vx[2] = tx + b / 2.0; vy[2] = ty + h / 3.0;    // 底右
                testEdge(vx[2], vy[2], vx[1], vy[1], 0);       // 顶边（上）
                testEdge(vx[2], vy[2], vx[0], vy[0], 1);       // 右斜
                testEdge(vx[0], vy[0], vx[1], vy[1], 2);       // 左斜
            }
            break;
        }
        default: {
            ensureTable();
            if (!table_) return -1;
            const int n = neighborCount(index);
            for (int k = 0; k < n; ++k) {
                double ax, ay, bx, by;
                if (cellEdge(index, k, ax, ay, bx, by))
                    testEdge(ax, ay, bx, by, k);
            }
            break;
        }
    }

    if (bestK < 0) return -1;  // 无前向边命中（位置贴边/界外 → 调用方 nudge 重定位）
    if (minT > remLength) {    // 剩余长度走完未撞边
        x += remLength * dx;
        y += remLength * dy;
        remLength = 0.0;
        return -2;
    }
    // 推进到命中点（顶点或边）。
    x = cx + minT * dx;
    y = cy + minT * dy;
    remLength -= minT;
    // 顶点命中（u ≈ 0 或 1）：位置已到顶点，返回 -1 → 调用方沿方向 nudge ε 穿越顶点
    //（确定性；直接取某条边会进入错误格——顶点是 3（六）/6（三）格共点）。
    constexpr double kVertexTol = 1e-9;
    if (minU <= kVertexTol || minU >= 1.0 - kVertexTol) return -1;
    return bestK;
}

// 表驱动密铺的"比例保持映射"参数（2026-08 依 .docs/地图尺寸比例映射.md 算法一离线求得，
// 由 tools/gen_table_domain_params.py 生成；改 tiling_specs_*.json 后须重跑该工具）。
// 每种密铺：s,t = 一块（周期域）的格行列数（s*t=B），u=wx/s、v=hy/t 为每格世界宽/高比例；
// p,q = 互质比例因子（p/q ≈ √(v/u)，使 (c*u)/(d*v) ≈ a/b）；Ra,Rb = 输入 a,b 须为的倍数
//（保证 c 为 s 倍数、d 为 t 倍数且 c,d 为整数；限制倍数尽量小，≤16 保证菜单步进可用；hex/tri
// 还会按需要扩大 Rb，使输出 rows 保持偶数）。
// 算法二（在线映射）：a'=ceil(a/Ra)*Ra、b'=ceil(b/Rb)*Rb；c=p·a'/q、d=q·b'/p；cols=c/s、rows=d/t。
// 关键：c ∝ a、d ∝ b（正比例）⇒ 调"长"只改 cols、调"宽"只改 rows（完全单调、方向一致）。
struct TableDomainParams {
    int s, t, p, q, Ra, Rb;
    bool forceEvenRows = false;
};
const TableDomainParams* tableDomainParams(TilingType t) {
    static const TableDomainParams kTable[static_cast<int>(kTilingTypeCount)] = {
        /* Square */ {1, 1, 1, 1, 1, 1, false},
        /* Hex */ {1, 1, 13, 14, 14, 13, true},
        /* Tri */ {2, 1, 4, 3, 3, 4, true},
        /* Arch33336 */ {6, 3, 2, 1, 3, 6, false},
        /* Arch33434 */ {2, 6, 5, 8, 16, 15, false},
        /* Arch3464 */ {3, 4, 9, 8, 8, 9, false},
        /* Arch3636 */ {3, 2, 8, 5, 15, 16, false},
        /* Arch31212 */ {3, 2, 8, 5, 15, 16, false},
        /* Arch4612 */ {3, 4, 2, 3, 9, 8, false},
        /* Arch488 */ {1, 2, 5, 7, 7, 10, false},
        /* Laves3636 */ {3, 2, 8, 5, 15, 16, false},
        /* Laves31212 */ {3, 4, 9, 8, 8, 9, false},
        /* Laves4612 */ {2, 12, 1, 2, 4, 6, false},
        /* Laves488 */ {2, 2, 1, 1, 2, 2, false},
        /* Laves33434 */ {2, 4, 3, 4, 8, 3, false},
        /* Laves33336 */ {3, 4, 9, 8, 8, 9, false},
        /* Laves3464 */ {3, 4, 9, 8, 8, 9, false},
    };
    const int i = static_cast<int>(t);
    return (i >= static_cast<int>(TilingType::Square) && i < static_cast<int>(kTilingTypeCount))
               ? &kTable[i]
               : nullptr;
}

int gcdInt(int a, int b) {
    while (b != 0) {
        const int r = a % b;
        a = b;
        b = r;
    }
    return std::abs(a);
}

int effectiveInputRowsMultiple(const TableDomainParams& prm) {
    if (!prm.forceEvenRows) return prm.Rb;
    // rows = (q / gcd(t,q)) * (inputHeight / Rb). If this factor is odd,
    // double the input multiple so the resulting periodic map has even rows.
    const int rowFactor = prm.q / gcdInt(prm.t, prm.q);
    return (rowFactor & 1) ? prm.Rb * 2 : prm.Rb;
}

bool tableInputRestriction(int tilingType, int& ra, int& rb) {
    const TilingType t = static_cast<TilingType>(tilingType);
    const TableDomainParams* prm = tableDomainParams(t);
    if (prm == nullptr || t == TilingType::Square) return false;
    ra = prm->Ra;
    rb = effectiveInputRowsMultiple(*prm);
    return true;
}

void chooseTableDomain(int tilingType, int userLength, int userWidth, int& cols, int& rows) {
    cols = std::max(1, userLength);
    rows = std::max(1, userWidth);
    const TilingType t = static_cast<TilingType>(tilingType);
    const TableDomainParams* prm = tableDomainParams(t);
    if (prm == nullptr) return;
    if (t == TilingType::Square) return;  // square 的用户尺寸就是周期域尺寸。
    // 算法二：输入合规化（四舍五入到最近的 Ra/Rb 倍数；若菜单已限制则原样）。
    const int Ra = prm->Ra, Rb = effectiveInputRowsMultiple(*prm);
    const int p = prm->p, q = prm->q, s = prm->s, tt = prm->t;
    auto snap = [](int v, int m, int cap) {
        const int base = std::max(1, m);
        int r = ((v + base / 2) / base) * base;
        if (r > cap) r = (cap / base) * base;
        return std::max(base, r);
    };
    const int a = snap(std::max(1, userLength), Ra, 200);
    const int b = snap(std::max(1, userWidth), Rb, 200);
    // c = p·a'/q（a' 为 Ra= q·s/gcd(s,p) 倍数 ⇒ q | a' ⇒ 整数）；d = q·b'/p（p | b' ⇒ 整数）。
    // cols = c/s、rows = d/t。守恒：B·cols·rows = s·t·(p·a'/q)/s·(q·b'/p)/t = a'·b'。
    const int c = p * a / q;
    const int d = q * b / p;
    cols = std::max(1, c / s);
    rows = std::max(1, d / tt);
}

}  // namespace lw
