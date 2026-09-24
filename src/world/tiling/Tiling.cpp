// Tiling.cpp — 密铺几何实现（P12 异种地图）。纯几何，RNG 0 次。
// 坐标/邻接/穿越语义见 Tiling.h 头注与开发计划 P12 §1-§2。
#include "world/tiling/Tiling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
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
// 表驱动周期块几何。
// 数据文件 data/tiling_specs_regular.json / data/tiling_specs_arch.json /
// data/tiling_specs_laves.json 按类别描述全部密铺。
// 基础域 = 周期平行四边形 W=(wx,0)、H=(hx,hy)；B 个基础格在域内按格下标
//   idx = (r*cols + c)*B + b 平移复制：center = base[b].center + c*W + r*H。
// 邻接在首次加载时按"某基础格的边经 ±dr,±dc 平移后与另一基础格的边反向重合"求得。
// 5.1 的规则类型预建半区/象限候选，5.2 的复杂类型预建周期坐标细网格；两者均以
// 预计算半平面作为精确判定，边界不明确时保留原扫描兜底。网格短方向默认 128 档，
// 周期向量长度明显较长的方向使用两倍密度（实验记录见 .docs/Phase5性能实验记录.md）。
// ---------------------------------------------------------------------------
struct TilingTable {
    double wx = 0.0, wy = 0.0, hx = 0.0, hy = 0.0;  // W=(wx,wy)、H=(hx,hy)（一般平行四边形周期域）
    double inv00 = 0.0, inv01 = 0.0, inv10 = 0.0, inv11 = 0.0;  // [W H]^-1
    double rx = 0.0, ry = 0.0;            // 基础格多边形相对中心的 AABB 半径（保守）
    // 基础格全部顶点在周期坐标 (u,v) 中的范围，以及 u/v 每单位对应的世界法向距离。
    double umin = 0.0, umax = 0.0, vmin = 0.0, vmax = 0.0;
    double uDistanceScale = 0.0, vDistanceScale = 0.0;
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
        std::vector<int> edgeOrder;            // 对外邻居序号 → 多边形边序号
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
    // ---- R1 顶点/边拓扑（河流系统；构建期一次，运行期只做整数运算）----
    struct VertexLink {
        int nb = -1;  // 共点基础格下标
        int j = 0;    // 该基础格上的顶点（多边形序）
        int dr = 0;   // 周期块行偏移
        int dc = 0;   // 周期块列偏移
    };
    // [b][i] → 顶点 (b,i) 的全部等价表示（含自身 (b,i,0,0)），按 (dr,dc,nb,j) 升序。
    // 自身表示不可省：规范键取 min 与"同格内相邻顶点"都要靠它。
    std::vector<std::vector<std::vector<VertexLink>>> vertexLinks;
    // [b][i] → 多边形序下的两个相邻顶点 {prev, next}（即两条入射边的对端）。
    std::vector<std::vector<std::array<int, 2>>> vertexAdj;
    // [b][k] → 对外邻居序 k 的反向边序号 k'（对侧格上的邻居序；-1 = 未找到）。
    std::vector<std::vector<int>> edgeReverseK;
    // [b] → 多边形边序号 → 对外邻居序（edgeOrder 的逆）。
    std::vector<std::vector<int>> edgePolyToK;
    // [b][pe] → 多边形边 pe 在**对侧基础格**上的多边形边序号（-1 = 未找到）。
    std::vector<std::vector<int>> edgeReversePoly;
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
    u = tab.inv00 * x + tab.inv01 * y;
    v = tab.inv10 * x + tab.inv11 * y;
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

bool usesTableGeometry(TilingType t) {
    const int value = static_cast<int>(t);
    return value >= static_cast<int>(TilingType::Square) && value < kTilingTypeCount;
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
    const int typeValue = static_cast<int>(t);
    const char* path = typeValue <= static_cast<int>(TilingType::Tri)
                           ? "data/tiling_specs_regular.json"
                       : typeValue < static_cast<int>(TilingType::Laves3636)
                           ? "data/tiling_specs_arch.json"
                           : "data/tiling_specs_laves.json";
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        spdlog::error("TilingTable: {} not found", path);
        return nullptr;
    }
    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const std::exception& e) {
        spdlog::error("TilingTable: failed to parse {}: {}", path, e.what());
        return nullptr;
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
        c.edgeOrder.resize(static_cast<size_t>(c.n));
        for (int k = 0; k < c.n; ++k) c.edgeOrder[static_cast<size_t>(k)] = k;
        if (jc.contains("edgeOrder")) {
            const auto& order = jc["edgeOrder"];
            if (!order.is_array() || static_cast<int>(order.size()) != c.n) {
                spdlog::error("TilingTable: invalid edgeOrder for '{}'", name);
                return nullptr;
            }
            std::vector<bool> seen(static_cast<size_t>(c.n), false);
            for (int k = 0; k < c.n; ++k) {
                const int edge = order[static_cast<size_t>(k)].get<int>();
                if (edge < 0 || edge >= c.n || seen[static_cast<size_t>(edge)]) {
                    spdlog::error("TilingTable: invalid edgeOrder for '{}'", name);
                    return nullptr;
                }
                seen[static_cast<size_t>(edge)] = true;
                c.edgeOrder[static_cast<size_t>(k)] = edge;
            }
        }
        tab->cells.push_back(std::move(c));
    }
    const double periodDet = tab->wx * tab->hy - tab->wy * tab->hx;
    const double wLength = std::hypot(tab->wx, tab->wy);
    const double hLength = std::hypot(tab->hx, tab->hy);
    if (std::fabs(periodDet) <= kTableTol || wLength <= kTableTol || hLength <= kTableTol) {
        spdlog::error("TilingTable: degenerate period vectors for '{}'", name);
        return nullptr;
    }
    tab->inv00 = tab->hy / periodDet;
    tab->inv01 = -tab->hx / periodDet;
    tab->inv10 = -tab->wy / periodDet;
    tab->inv11 = tab->wx / periodDet;
    tab->uDistanceScale = std::fabs(periodDet) / hLength;
    tab->vDistanceScale = std::fabs(periodDet) / wLength;
    for (const auto& cell : tab->cells) {
        for (const auto& vertex : cell.v) {
            double u = 0.0, v = 0.0;
            periodicCoordinates(*tab, vertex[0], vertex[1], u, v);
            tab->umin = std::min(tab->umin, u);
            tab->umax = std::max(tab->umax, u);
            tab->vmin = std::min(tab->vmin, v);
            tab->vmax = std::max(tab->vmax, v);
        }
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
    // 顶点/边拓扑（R1，河流系统）：把 (基础格, 顶点, dr, dc) 归并成等价类。
    // 顶点：±2 周期窗口内与之重合的全部 (nb,j,dr,dc)（窗口与上面 vertexNeighbors 一致）；
    //       含自身表示，运行期靠它取规范键与"同格内相邻顶点"。
    // 边：edgeOrder 的逆映射 + 反向边（判据与 :511 邻接构建完全相同）+ 对外邻居序版本。
    tab->vertexLinks.assign(static_cast<size_t>(B), {});
    tab->vertexAdj.assign(static_cast<size_t>(B), {});
    tab->edgeReverseK.assign(static_cast<size_t>(B), {});
    tab->edgePolyToK.assign(static_cast<size_t>(B), {});
    tab->edgeReversePoly.assign(static_cast<size_t>(B), {});
    // edgeOrder 的逆映射须先对全部基础格建好：下面反向边要跨基础格查对侧的 polygon→k。
    for (int b = 0; b < B; ++b) {
        const auto& cell = tab->cells[static_cast<size_t>(b)];
        tab->edgePolyToK[static_cast<size_t>(b)].assign(static_cast<size_t>(cell.n), -1);
        for (int k = 0; k < cell.n; ++k)
            tab->edgePolyToK[static_cast<size_t>(b)][static_cast<size_t>(
                cell.edgeOrder[static_cast<size_t>(k)])] = k;
    }
    for (int b = 0; b < B; ++b) {
        const auto& cell = tab->cells[static_cast<size_t>(b)];
        auto& links = tab->vertexLinks[static_cast<size_t>(b)];
        auto& adj = tab->vertexAdj[static_cast<size_t>(b)];
        links.resize(static_cast<size_t>(cell.n));
        adj.resize(static_cast<size_t>(cell.n));
        for (int i = 0; i < cell.n; ++i) {
            adj[static_cast<size_t>(i)] = {(i + cell.n - 1) % cell.n, (i + 1) % cell.n};
            const auto& p = cell.v[static_cast<size_t>(i)];
            auto& list = links[static_cast<size_t>(i)];
            for (int dr = -2; dr <= 2; ++dr) {
                for (int dc = -2; dc <= 2; ++dc) {
                    const double ox = static_cast<double>(dc) * tab->wx
                                      + static_cast<double>(dr) * tab->hx;
                    const double oy = static_cast<double>(dc) * tab->wy
                                      + static_cast<double>(dr) * tab->hy;
                    for (int nb = 0; nb < B; ++nb) {
                        const auto& other = tab->cells[static_cast<size_t>(nb)];
                        for (int j = 0; j < other.n; ++j) {
                            if (std::fabs(p[0] - (other.v[static_cast<size_t>(j)][0] + ox))
                                    <= kTableTol &&
                                std::fabs(p[1] - (other.v[static_cast<size_t>(j)][1] + oy))
                                    <= kTableTol)
                                list.push_back({nb, j, dr, dc});
                        }
                    }
                }
            }
            std::sort(list.begin(), list.end(), [](const TilingTable::VertexLink& x,
                                                   const TilingTable::VertexLink& y) {
                if (x.dr != y.dr) return x.dr < y.dr;
                if (x.dc != y.dc) return x.dc < y.dc;
                if (x.nb != y.nb) return x.nb < y.nb;
                return x.j < y.j;
            });
        }
        tab->edgeReversePoly[static_cast<size_t>(b)].assign(static_cast<size_t>(cell.n), -1);
        tab->edgeReverseK[static_cast<size_t>(b)].assign(static_cast<size_t>(cell.n), -1);
        for (int pe = 0; pe < cell.n; ++pe) {
            const auto& e = cell.edges[static_cast<size_t>(pe)];
            if (e.nb < 0) continue;
            const auto& oc = tab->cells[static_cast<size_t>(e.nb)];
            const std::array<double, 2> a = cell.v[static_cast<size_t>(pe)];
            const std::array<double, 2> bpt = cell.v[static_cast<size_t>((pe + 1) % cell.n)];
            // 与 :511 邻接构建同一判据、同一偏移：把对侧格的顶点按本侧记录到的 (dr,dc)
            // 平移过来，反向重合的那条边即对侧的多边形边。
            const double ox = static_cast<double>(e.dc) * tab->wx
                              + static_cast<double>(e.dr) * tab->hx;
            const double oy = static_cast<double>(e.dc) * tab->wy
                              + static_cast<double>(e.dr) * tab->hy;
            for (int kk = 0; kk < oc.n; ++kk) {
                if (oc.edges[static_cast<size_t>(kk)].nb != b) continue;
                const std::array<double, 2> n0 = {oc.v[static_cast<size_t>(kk)][0] + ox,
                                                  oc.v[static_cast<size_t>(kk)][1] + oy};
                const std::array<double, 2> n1 = {
                    oc.v[static_cast<size_t>((kk + 1) % oc.n)][0] + ox,
                    oc.v[static_cast<size_t>((kk + 1) % oc.n)][1] + oy};
                if (samePoint(n0, bpt) && samePoint(n1, a)) {
                    tab->edgeReversePoly[static_cast<size_t>(b)][static_cast<size_t>(pe)] = kk;
                    break;
                }
            }
            const int k = tab->edgePolyToK[static_cast<size_t>(b)][static_cast<size_t>(pe)];
            const int rpe = tab->edgeReversePoly[static_cast<size_t>(b)][static_cast<size_t>(pe)];
            if (k >= 0 && rpe >= 0)
                tab->edgeReverseK[static_cast<size_t>(b)][static_cast<size_t>(k)] =
                    tab->edgePolyToK[static_cast<size_t>(e.nb)][static_cast<size_t>(rpe)];
        }
    }
    buildFastIndexes(*tab, t);
    fillTilePalette(*tab, t);
    return tab;
}

// ---- R1 顶点/边拓扑：运行期整数查询（无浮点比较）----
// 键编码 = 1 + cell * 键宽 + idx（0 = 无效）。
std::uint64_t topoVertexKey(int cell, int v) {
    return 1ull + static_cast<std::uint64_t>(cell) * TilingGeom::kMaxCellVerts
           + static_cast<std::uint64_t>(v);
}

std::uint64_t topoEdgeKey(int cell, int k) {
    return 1ull + static_cast<std::uint64_t>(cell) * TilingGeom::kMaxCellEdges
           + static_cast<std::uint64_t>(k);
}

// 顶点的一个已知表示（周期块 (row,col) 上的基础格 b 的顶点 v）→ 规范键：
// 枚举其等价类中全部图内表示，取 (格下标, 顶点序号) 字典序最小者。0 = 无图内表示。
// 一致性依据：同一几何顶点的等价类跨度 ≤ 1 个周期块，小于 vertexLinks 的 ±2 窗口，
// 故任取类内一个表示都能枚举到整个类 → 不同表示给出同一 min（《河流系统开发文档》§4.4）。
std::uint64_t canonicalVertexKey(const TilingTable& tab, int cols, int rows, int row, int col,
                                 int b, int v) {
    const int B = static_cast<int>(tab.cells.size());
    int bestCell = -1, bestVert = -1;
    for (const auto& link : tab.vertexLinks[static_cast<size_t>(b)][static_cast<size_t>(v)]) {
        const int nr = row + link.dr, nc = col + link.dc;
        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
        const int cell = (nr * cols + nc) * B + link.nb;
        if (bestCell < 0 || cell < bestCell || (cell == bestCell && link.j < bestVert)) {
            bestCell = cell;
            bestVert = link.j;
        }
    }
    return bestCell < 0 ? 0 : topoVertexKey(bestCell, bestVert);
}

// 顶点 (index,v) 的邻点（= 图内入射边的对端顶点）与连接边。
struct VertexNeighborEntry {
    int cell = -1;      // 对端顶点在连接边所属格上的表示（多边形顶点序）
    int vert = 0;
    int edgeCell = -1;  // 连接边的规范表示：格 + 格内边序号（与 neighbor/cellEdge 同序）
    int edgeK = 0;
    std::uint64_t key = 0;  // 对端顶点的规范顶点键
};
// 单个顶点的邻点数上限：格内边数 ≤ 12（当前最大 12-gon），故邻点 ≤ 12；留余量。
constexpr int kMaxVertexNeighbors = 32;

// 枚举 (index,v) 的全部邻点：遍历 vertexLinks 的全部图内表示 × 该表示的两条入射边。
// 每条几何边会被两侧格各枚举一次（对侧在图外则一次），按对端顶点的规范键去重。
// 返回条数（≤ kMaxVertexNeighbors）；顶点无效返回 0。
int collectVertexNeighbors(const TilingTable& tab, int cols, int rows, int index, int v,
                           VertexNeighborEntry* out) {
    const int B = static_cast<int>(tab.cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int row = rc / cols, col = rc % cols;
    const auto& cell = tab.cells[static_cast<size_t>(b)];
    if (v < 0 || v >= cell.n) return 0;
    int count = 0;
    for (const auto& link : tab.vertexLinks[static_cast<size_t>(b)][static_cast<size_t>(v)]) {
        const int nr = row + link.dr, nc = col + link.dc;
        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
        const int ownCell = (nr * cols + nc) * B + link.nb;
        const auto& own = tab.cells[static_cast<size_t>(link.nb)];
        const int n = own.n;
        for (int side = 0; side < 2; ++side) {
            // 本顶点所在的两条多边形边：side 0 = (j-1 → j)，side 1 = (j → j+1)。
            const int pe = side == 0 ? (link.j + n - 1) % n : link.j;
            const auto& e = own.edges[static_cast<size_t>(pe)];
            if (e.nb < 0) continue;
            const int wr = nr + e.dr, wc = nc + e.dc;
            const auto& other = tab.cells[static_cast<size_t>(e.nb)];
            const int rpe = tab.edgeReversePoly[static_cast<size_t>(link.nb)][static_cast<size_t>(pe)];
            if (rpe < 0) continue;
            const int kOwn = tab.edgePolyToK[static_cast<size_t>(link.nb)][static_cast<size_t>(pe)];
            if (kOwn < 0) continue;
            // 对端顶点：本格 pe 的远端（side 0 = v[j-1]，side 1 = v[j+1]，取自 vertexAdj）。
            const int ownVert = tab.vertexAdj[static_cast<size_t>(link.nb)][static_cast<size_t>(link.j)]
                                    [static_cast<size_t>(side)];
            int outCell, outVert, edgeCell, edgeK;
            if (wr < 0 || wr >= rows || wc < 0 || wc >= cols) {
                // 地图边界边：对侧在图外，只保留本侧编码；对端仍是本格顶点（图内）。
                edgeCell = ownCell;
                edgeK = kOwn;
                outCell = ownCell;
                outVert = ownVert;
            } else {
                const int kOther =
                    tab.edgePolyToK[static_cast<size_t>(e.nb)][static_cast<size_t>(rpe)];
                if (kOther < 0) continue;
                const int otherCell = (wr * cols + wc) * B + e.nb;
                if (otherCell < ownCell || (otherCell == ownCell && kOther < kOwn)) {
                    edgeCell = otherCell;
                    edgeK = kOther;
                    outCell = otherCell;
                    outVert = side == 0 ? (rpe + 1) % other.n : rpe;
                } else {
                    edgeCell = ownCell;
                    edgeK = kOwn;
                    outCell = ownCell;
                    outVert = ownVert;
                }
            }
            const std::uint64_t key =
                (outCell == ownCell) ? canonicalVertexKey(tab, cols, rows, nr, nc, link.nb, outVert)
                                     : canonicalVertexKey(tab, cols, rows, wr, wc, e.nb, outVert);
            if (key == 0) continue;
            bool duplicate = false;
            for (int i = 0; i < count; ++i) {
                if (out[i].key == key) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            out[count] = {outCell, outVert, edgeCell, edgeK, key};
            ++count;
        }
    }
    return count;
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
        if (fu <= kFastBoundaryTol || fu >= 1.0 - kFastBoundaryTol ||
            fv <= kFastBoundaryTol || fv >= 1.0 - kFastBoundaryTol)
            return -1;
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
    const auto& candidate = tab.lookupGrid[static_cast<size_t>(gy * tab.lookupGridWidth + gx)];
    const int rr = row + candidate.dr, cc = col + candidate.dc;
    if (candidate.b < 0 || rr < 0 || rr >= rows || cc < 0 || cc >= cols) return -1;
    // 构建时已证明整个细网格桶严格位于同一凸格内，运行时无需再次做半平面校验。
    return (rr * cols + cc) * static_cast<int>(tab.cells.size()) + candidate.b;
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
    for (int b = 0; b < baseCount; ++b) {
        const auto& cell = tab.cells[static_cast<size_t>(b)];
        const double px = x - cell.cx, py = y - cell.cy;
        const double c0f = tab.inv00 * px + tab.inv01 * py;
        const double r0f = tab.inv10 * px + tab.inv11 * py;
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

int tryRegularCell(const TilingTable& tab, TilingType type, int cols, int rows, double x,
                   double y) {
    constexpr double kBoundaryTol = 4.0 * kTableTol;
    if (type == TilingType::Hex && tab.cells.size() == 2) {
        const double rowSpacing = 0.5 * tab.hy;
        const int nearestRow =
            static_cast<int>(std::lround((y - tab.cells[0].cy) / rowSpacing));
        for (int offset : {0, -1, 1}) {
            const int physicalRow = nearestRow + offset;
            if (physicalRow < 0 || physicalRow >= 2 * rows) continue;
            const int base = physicalRow & 1;
            const int row = physicalRow / 2;
            const auto& cell = tab.cells[static_cast<size_t>(base)];
            const int col = static_cast<int>(std::lround((x - cell.cx) / tab.wx));
            if (col < 0 || col >= cols) continue;
            const double cx = cell.cx + static_cast<double>(col) * tab.wx;
            const double cy = cell.cy + static_cast<double>(row) * tab.hy;
            const double ay = std::fabs(y - cy);
            const double xLimit = std::min(0.5 * tab.wx,
                                           tab.wx * (1.0 - ay / TilingGeom::kHexSide));
            if (ay < TilingGeom::kHexSide - kBoundaryTol &&
                std::fabs(x - cx) < xLimit - kBoundaryTol)
                return (row * cols + col) * 2 + base;
        }
        return -1;
    }

    if (type == TilingType::Tri && tab.cells.size() == 4) {
        const double stripHeight = 0.5 * tab.hy;
        const double rowValue = (y - tab.ymin) / stripHeight;
        const int physicalRow = static_cast<int>(std::floor(rowValue));
        if (physicalRow < 0 || physicalRow >= 2 * rows) return -1;
        const double t = rowValue - static_cast<double>(physicalRow);
        const double yTol = kBoundaryTol / stripHeight;
        if (t <= yTol || t >= 1.0 - yTol) return -1;
        const int parity = physicalRow & 1;
        const double shiftedX = (x - tab.xmin) / tab.wx - 0.5 * parity;
        const int interval = static_cast<int>(std::floor(shiftedX));
        const double fraction = shiftedX - static_cast<double>(interval);
        const double edge = 0.5 * t;
        const double xTol = kBoundaryTol / tab.wx;
        bool up = false;
        int col = interval;
        if (fraction > edge + xTol && fraction < 1.0 - edge - xTol) {
            up = true;
        } else if (fraction < edge - xTol) {
            col += parity == 0 ? -1 : 0;
        } else if (fraction > 1.0 - edge + xTol) {
            col += parity == 0 ? 0 : 1;
        } else {
            return -1;
        }
        if (col < 0 || col >= cols) return -1;
        const int row = physicalRow / 2;
        const int base = 2 * parity + (up ? 0 : 1);
        return (row * cols + col) * 4 + base;
    }
    return -1;
}

}  // namespace

void TilingGeom::ensureTable() const {
    if (!usesTableGeometry(type) || table_) return;
    static std::array<std::once_flag, kTilingTypeCount> once;
    static std::array<std::shared_ptr<const TilingTable>, kTilingTypeCount> cache;
    const int index = static_cast<int>(type);
    std::call_once(once[static_cast<size_t>(index)], [index] {
        cache[static_cast<size_t>(index)] = loadTable(static_cast<TilingType>(index));
    });
    table_ = cache[static_cast<size_t>(index)];
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
    ensureTable();
    if (!table_) return 0.0;
    const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
    return table_->xmax - table_->xmin + std::fabs(cmax * table_->wx) +
           std::fabs(rmax * table_->hx);
}

double TilingGeom::worldHeight() const {
    ensureTable();
    if (!table_) return 0.0;
    const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
    return table_->ymax - table_->ymin + std::fabs(cmax * table_->wy) +
           std::fabs(rmax * table_->hy);
}

// 世界坐标域原点（AABB 下界）：格顶点 x = base.v.x + c*wx + r*hx。对表驱动密铺（含 overhang
// 与 W.y/H.x 剪切），四角 (c,r) ∈{0,cols-1}x{0,rows-1} 的偏移极值 + 基础格 AABB 下界 = 域左下角。
// cellCenter/cellPolygon 统一做 -worldMin 平移，把世界坐标归到 [0,worldWidth]x[0,worldHeight]。
// 这保证相机/兵特效/空间哈希的 [0,·] 夹取成立（2026-08：含 arch_33434 块整体左移后的负中心格）。
double TilingGeom::worldMinX() const {
    ensureTable();
    if (!table_) return 0.0;
    const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
    return table_->xmin + std::min(0.0, cmax * table_->wx) +
           std::min(0.0, rmax * table_->hx);
}

double TilingGeom::worldMinY() const {
    ensureTable();
    if (!table_) return 0.0;
    const double cmax = static_cast<double>(cols - 1), rmax = static_cast<double>(rows - 1);
    return table_->ymin + std::min(0.0, cmax * table_->wy) +
           std::min(0.0, rmax * table_->hy);
}

bool TilingGeom::hasSkewedPeriod() const {
    if (!isTableType(type)) return false;
    ensureTable();
    return table_ && (std::fabs(table_->wy) > kEps || std::fabs(table_->hx) > kEps);
}

int TilingGeom::neighborCount() const {
    ensureTable();
    int m = 0;
    if (table_) for (const auto& c : table_->cells) m = std::max(m, c.n);
    return m;
}

int TilingGeom::neighborCount(int index) const {
    ensureTable();
    if (!table_ || index < 0 || index >= cellCount()) return 0;
    const int B = static_cast<int>(table_->cells.size());
    return table_->cells[static_cast<size_t>(index % B)].n;
}

int TilingGeom::baseCount() const {
    ensureTable();
    return table_ ? static_cast<int>(table_->cells.size()) : 0;
}

int TilingGeom::cellIndexAt(int r, int c, int b) const {
    const int B = baseCount();
    if (B <= 0 || r < 0 || r >= rows || c < 0 || c >= cols) return -1;
    if (b < 0 || b >= B) return -1;
    return (r * cols + c) * B + b;
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
    ensureTable();
    if (!table_ || index < 0 || index >= cellCount()) {
        wx = wy = 0.0;
        return;
    }
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
}

int TilingGeom::cellPolygon(int index, double* wx, double* wy, int maxVerts) const {
    ensureTable();
    if (!table_ || index < 0 || index >= cellCount()) return 0;
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
    const double wx = table_->wx, wy = table_->wy, hx = table_->hx, hy = table_->hy;
    for (int i = 0; i < cell.n; ++i) {
        // 未平移世界顶点 = cell.v + c*W + r*H（-worldMin 已由 cellPolygon 抵消，这里用未平移帧）。
        const double ux = cell.v[static_cast<size_t>(i)][0] + static_cast<double>(c) * wx
                          + static_cast<double>(r) * hx;
        const double uy = cell.v[static_cast<size_t>(i)][1] + static_cast<double>(c) * wy
                          + static_cast<double>(r) * hy;
        gx[i] = table_->inv00 * ux + table_->inv01 * uy;
        gy[i] = table_->inv10 * ux + table_->inv11 * uy;
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
    const double wx = table_->wx, wy = table_->wy, hx = table_->hx, hy = table_->hy;
    const double ux = cell.cx + static_cast<double>(c) * wx + static_cast<double>(r) * hx;
    const double uy = cell.cy + static_cast<double>(c) * wy + static_cast<double>(r) * hy;
    gx = table_->inv00 * ux + table_->inv01 * uy;
    gy = table_->inv10 * ux + table_->inv11 * uy;
}

double TilingGeom::cellBoundaryDistance(int index) const {
    ensureTable();
    if (!table_ || index < 0 || index >= cellCount()) return 0.0;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int row = rc / cols;
    const int col = rc % cols;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    const double mapUmax = table_->umax + static_cast<double>(cols - 1);
    const double mapVmax = table_->vmax + static_cast<double>(rows - 1);
    double distance = std::numeric_limits<double>::max();
    for (const auto& vertex : cell.v) {
        double u = 0.0, v = 0.0;
        periodicCoordinates(*table_, vertex[0], vertex[1], u, v);
        u += static_cast<double>(col);
        v += static_cast<double>(row);
        distance = std::min(distance, (u - table_->umin) * table_->uDistanceScale);
        distance = std::min(distance, (mapUmax - u) * table_->uDistanceScale);
        distance = std::min(distance, (v - table_->vmin) * table_->vDistanceScale);
        distance = std::min(distance, (mapVmax - v) * table_->vDistanceScale);
    }
    return std::max(0.0, distance);
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
    ensureTable();
    if (!table_) return false;
    const int B = static_cast<int>(table_->cells.size());
    const auto& cell = table_->cells[static_cast<size_t>(index % B)];
    const int edge = cell.edgeOrder[static_cast<size_t>(k)];
    x0 = vx[edge];
    y0 = vy[edge];
    x1 = vx[(edge + 1) % n];
    y1 = vy[(edge + 1) % n];
    return true;
}

int TilingGeom::worldToCell(double wx, double wy) const {
    ensureTable();
    if (!table_) return -1;
    const double ux = wx + worldMinX(), uy = wy + worldMinY();
    if (!isTableType(type)) {
        if (table_->cells.size() == 1) {
            double u = 0.0, v = 0.0;
            periodicCoordinates(*table_, ux, uy, u, v);
            const int col = static_cast<int>(std::floor(u));
            const int row = static_cast<int>(std::floor(v));
            return row >= 0 && row < rows && col >= 0 && col < cols ? row * cols + col : -1;
        }
        const int regular = tryRegularCell(*table_, type, cols, rows, ux, uy);
        if (regular >= 0) return regular;
    } else {
        const int fast = tryFastTableCell(*table_, type, cols, rows, ux, uy);
        if (fast >= 0) return fast;
    }
    return scanTableCell(*table_, cols, rows, wx, wy, ux, uy, worldWidth(), worldHeight());
}

void TilingGeom::rowRange(double x0, double y0, double x1, double y1, int& r0, int& r1) const {
    ensureTable();
    if (!table_) {
        r0 = 0;
        r1 = rows - 1;
    } else if (std::fabs(table_->wy) <= kEps) {
        r0 = static_cast<int>(std::floor((std::min(y0, y1) - table_->ry) / table_->hy)) - 1;
        r1 = static_cast<int>(std::ceil((std::max(y0, y1) + table_->ry) / table_->hy)) + 1;
    } else {
        const double ox = worldMinX(), oy = worldMinY();
        const double rawX[2] = {std::min(x0, x1) + ox, std::max(x0, x1) + ox};
        const double rawY[2] = {std::min(y0, y1) + oy, std::max(y0, y1) + oy};
        double vmin = std::numeric_limits<double>::max();
        double vmax = std::numeric_limits<double>::lowest();
        for (double x : rawX) {
            for (double y : rawY) {
                const double v = table_->inv10 * x + table_->inv11 * y;
                vmin = std::min(vmin, v);
                vmax = std::max(vmax, v);
            }
        }
        r0 = static_cast<int>(std::floor(vmin - table_->vmax)) - 1;
        r1 = static_cast<int>(std::ceil(vmax - table_->vmin)) + 1;
    }
    r0 = std::clamp(r0, 0, std::max(0, rows - 1));
    r1 = std::clamp(r1, 0, std::max(0, rows - 1));
}

void TilingGeom::colRange(double x0, double x1, int r, int& c0, int& c1) const {
    ensureTable();
    if (table_) {
        const double ox = worldMinX();
        const double baseX = static_cast<double>(r) * table_->hx;
        c0 = static_cast<int>(std::floor((x0 + ox - baseX - table_->rx) / table_->wx)) - 1;
        c1 = static_cast<int>(std::ceil((x1 + ox - baseX + table_->rx) / table_->wx)) + 1;
    } else {
        c0 = 0;
        c1 = cols - 1;
    }
    c0 = std::clamp(c0, 0, std::max(0, cols - 1));
    c1 = std::clamp(c1, 0, std::max(0, cols - 1));
}

void TilingGeom::neighborRaw(int index, int k, int& r, int& c) const {
    ensureTable();
    if (!table_ || index < 0 || index >= cellCount()) {
        r = c = -1;
        return;
    }
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    if (k < 0 || k >= cell.n) {
        r = c = -1;
        return;
    }
    const auto& edge = cell.edges[static_cast<size_t>(
        cell.edgeOrder[static_cast<size_t>(k)])];
    r = rc / cols + edge.dr;
    c = rc % cols + edge.dc;
}

int TilingGeom::neighbor(int index, int k) const {
    if (index < 0 || k < 0 || k >= neighborCount(index)) return -1;
    int r, c;
    neighborRaw(index, k, r, c);
    if (r < 0 || r >= rows || c < 0 || c >= cols) return -1;
    ensureTable();
    if (!table_) return -1;
    const int B = static_cast<int>(table_->cells.size());
    const auto& cell = table_->cells[static_cast<size_t>(index % B)];
    const auto& edge = cell.edges[static_cast<size_t>(
        cell.edgeOrder[static_cast<size_t>(k)])];
    return (r * cols + c) * B + edge.nb;
}

int TilingGeom::pointNeighborCount(int index) const {
    if (index < 0 || index >= cellCount()) return 0;
    ensureTable();
    if (!table_ || table_->cells.empty()) return 0;
    return static_cast<int>(
        table_->vertexNeighbors[static_cast<size_t>(index % table_->cells.size())].size());
}

int TilingGeom::pointNeighbor(int index, int k) const {
    if (index < 0 || index >= cellCount() || k < 0 || k >= pointNeighborCount(index)) return -1;
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

// ================= R1 顶点/边拓扑（《河流系统开发文档》§4）=================
// 全部为整数拓扑查询：构建期在 TilingTable 里建立等价类与反向边表，运行期零浮点比较。

int TilingGeom::cellVertexCount(int index) const {
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return 0;
    return table_->cells[static_cast<size_t>(index % table_->cells.size())].n;
}

void TilingGeom::cellVertex(int index, int v, double& wx, double& wy) const {
    wx = wy = 0.0;
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int r = rc / cols, c = rc % cols;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    if (v < 0 || v >= cell.n) return;
    wx = cell.v[static_cast<size_t>(v)][0] + static_cast<double>(c) * table_->wx
         + static_cast<double>(r) * table_->hx - worldMinX();
    wy = cell.v[static_cast<size_t>(v)][1] + static_cast<double>(c) * table_->wy
         + static_cast<double>(r) * table_->hy - worldMinY();
}

void TilingGeom::gridVertex(int index, int v, double& gx, double& gy) const {
    if (!hasSkewedPeriod()) {
        cellVertex(index, v, gx, gy);
        return;
    }
    gx = gy = 0.0;
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int r = rc / cols, c = rc % cols;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    if (v < 0 || v >= cell.n) return;
    const double ux = cell.v[static_cast<size_t>(v)][0] + static_cast<double>(c) * table_->wx
                      + static_cast<double>(r) * table_->hx;
    const double uy = cell.v[static_cast<size_t>(v)][1] + static_cast<double>(c) * table_->wy
                      + static_cast<double>(r) * table_->hy;
    gx = table_->inv00 * ux + table_->inv01 * uy;
    gy = table_->inv10 * ux + table_->inv11 * uy;
}

std::uint64_t TilingGeom::vertexKey(int index, int v) const {
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return 0;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int row = rc / cols, col = rc % cols;
    if (v < 0 || v >= table_->cells[static_cast<size_t>(b)].n) return 0;
    return canonicalVertexKey(*table_, cols, rows, row, col, b, v);
}

bool TilingGeom::vertexFromKey(std::uint64_t key, int& index, int& v) const {
    index = -1;
    v = -1;
    if (key == 0) return false;
    ensureTable();
    if (!table_ || table_->cells.empty()) return false;
    const std::uint64_t raw = key - 1;
    const int cell = static_cast<int>(raw / TilingGeom::kMaxCellVerts);
    const int vert = static_cast<int>(raw % TilingGeom::kMaxCellVerts);
    if (cell < 0 || cell >= cellCount()) return false;
    if (vert < 0 || vert >= table_->cells[static_cast<size_t>(cell % table_->cells.size())].n)
        return false;
    index = cell;
    v = vert;
    return true;
}

int TilingGeom::vertexNeighborCount(int index, int v) const {
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return 0;
    VertexNeighborEntry entries[kMaxVertexNeighbors];
    return collectVertexNeighbors(*table_, cols, rows, index, v, entries);
}

int TilingGeom::vertexNeighbor(int index, int v, int k, int& outCell, int& outVert) const {
    outCell = -1;
    outVert = -1;
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return -1;
    VertexNeighborEntry entries[kMaxVertexNeighbors];
    const int count = collectVertexNeighbors(*table_, cols, rows, index, v, entries);
    if (k < 0 || k >= count) return -1;
    outCell = entries[k].cell;
    outVert = entries[k].vert;
    return k;
}

std::uint64_t TilingGeom::vertexNeighborKey(int index, int v, int k) const {
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return 0;
    VertexNeighborEntry entries[kMaxVertexNeighbors];
    const int count = collectVertexNeighbors(*table_, cols, rows, index, v, entries);
    if (k < 0 || k >= count) return 0;
    return entries[k].key;
}

int TilingGeom::vertexNeighborEdge(int index, int v, int k, int& outCell, int& outK) const {
    outCell = -1;
    outK = -1;
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return -1;
    VertexNeighborEntry entries[kMaxVertexNeighbors];
    const int count = collectVertexNeighbors(*table_, cols, rows, index, v, entries);
    if (k < 0 || k >= count) return -1;
    outCell = entries[k].edgeCell;
    outK = entries[k].edgeK;
    return k;
}

int TilingGeom::cellVertexEdgeCount(int index, int v) const {
    const int n = cellVertexCount(index);
    if (n <= 0 || v < 0 || v >= n) return 0;
    return 2;
}

int TilingGeom::cellVertexEdge(int index, int v, int which) const {
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return -1;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    if (v < 0 || v >= cell.n || which < 0 || which > 1) return -1;
    // 两条入射边：which 0 → 多边形边 (v-1 → v)，which 1 → (v → v+1)。
    // 对端顶点即 vertexAdj[b][v][which]（前一/后一顶点）。
    const int pe = which == 0 ? (v + cell.n - 1) % cell.n : v;
    return table_->edgePolyToK[static_cast<size_t>(b)][static_cast<size_t>(pe)];
}

bool TilingGeom::cellEdgeVertices(int index, int k, int& vA, int& vB) const {
    vA = -1;
    vB = -1;
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return false;
    const auto& cell = table_->cells[static_cast<size_t>(index % table_->cells.size())];
    if (k < 0 || k >= cell.n) return false;
    const int pe = cell.edgeOrder[static_cast<size_t>(k)];
    vA = pe;
    vB = (pe + 1) % cell.n;
    return true;
}

int TilingGeom::edgeIndexBetweenVertices(int index, int v, int neighborCell,
                                         int neighborVert) const {
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return -1;
    const std::uint64_t a = vertexKey(index, v);
    const std::uint64_t b = vertexKey(neighborCell, neighborVert);
    if (a == 0 || b == 0 || a == b) return -1;
    const int n = neighborCount(index);
    for (int k = 0; k < n; ++k) {
        if (neighbor(index, k) != neighborCell) continue;
        int vA = -1, vB = -1;
        if (!cellEdgeVertices(index, k, vA, vB)) continue;
        const std::uint64_t ka = vertexKey(index, vA);
        const std::uint64_t kb = vertexKey(index, vB);
        if ((ka == a && kb == b) || (ka == b && kb == a)) return k;
    }
    return -1;
}

std::uint64_t TilingGeom::edgeKey(int index, int k) const {
    ensureTable();
    if (!table_ || table_->cells.empty() || index < 0 || index >= cellCount()) return 0;
    const int B = static_cast<int>(table_->cells.size());
    const int b = index % B;
    const int rc = index / B;
    const int row = rc / cols, col = rc % cols;
    const auto& cell = table_->cells[static_cast<size_t>(b)];
    if (k < 0 || k >= cell.n) return 0;
    const auto& e = cell.edges[static_cast<size_t>(cell.edgeOrder[static_cast<size_t>(k)])];
    if (e.nb < 0) return 0;
    const int nr = row + e.dr, nc = col + e.dc;
    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) return topoEdgeKey(index, k);  // 地图边界
    const int otherCell = (nr * cols + nc) * B + e.nb;
    const int kk = table_->edgeReverseK[static_cast<size_t>(b)][static_cast<size_t>(k)];
    if (kk < 0) return 0;
    if (otherCell < index || (otherCell == index && kk < k)) return topoEdgeKey(otherCell, kk);
    return topoEdgeKey(index, k);
}

std::uint64_t TilingGeom::edgeKeyBetween(int index, int neighborIndex) const {
    if (neighborIndex < 0) return 0;
    const int n = neighborCount(index);
    for (int k = 0; k < n; ++k)
        if (neighbor(index, k) == neighborIndex) return edgeKey(index, k);
    return 0;
}

bool TilingGeom::edgeFromKey(std::uint64_t key, int& index, int& k) const {
    index = -1;
    k = -1;
    if (key == 0) return false;
    ensureTable();
    if (!table_ || table_->cells.empty()) return false;
    const std::uint64_t raw = key - 1;
    const int cell = static_cast<int>(raw / TilingGeom::kMaxCellEdges);
    const int edge = static_cast<int>(raw % TilingGeom::kMaxCellEdges);
    if (cell < 0 || cell >= cellCount()) return false;
    if (edge < 0 || edge >= table_->cells[static_cast<size_t>(cell % table_->cells.size())].n)
        return false;
    index = cell;
    k = edge;
    return true;
}

bool TilingGeom::edgeEndpoints(std::uint64_t key, double& x0, double& y0, double& x1,
                               double& y1) const {
    int index = -1, k = -1;
    if (!edgeFromKey(key, index, k)) return false;
    return cellEdge(index, k, x0, y0, x1, y1);
}

void TilingGeom::edgeCells(int index, int k, int& outA, int& outB) const {
    outA = -1;
    outB = -1;
    const std::uint64_t key = edgeKey(index, k);
    if (key == 0) return;
    int a = -1, ka = -1;
    if (!edgeFromKey(key, a, ka)) return;
    outA = a;
    outB = neighbor(a, ka);
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

    ensureTable();
    if (!table_) return -1;
    const int n = neighborCount(index);
    for (int k = 0; k < n; ++k) {
        double ax, ay, bx, by;
        if (cellEdge(index, k, ax, ay, bx, by))
            testEdge(ax, ay, bx, by, k);
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
//（保证 c 为 s 倍数、d 为 t 倍数且 c,d 为整数；限制倍数尽量小，≤16 保证菜单步进可用）。
// 算法二（在线映射）：a'=ceil(a/Ra)*Ra、b'=ceil(b/Rb)*Rb；c=p·a'/q、d=q·b'/p；cols=c/s、rows=d/t。
// 关键：c ∝ a、d ∝ b（正比例）⇒ 调"长"只改 cols、调"宽"只改 rows（完全单调、方向一致）。
struct TableDomainParams {
    int s, t, p, q, Ra, Rb;
};
const TableDomainParams* tableDomainParams(TilingType t) {
    static const TableDomainParams kTable[static_cast<int>(kTilingTypeCount)] = {
        /* Square */ {1, 1, 1, 1, 1, 1},
        /* Hex */ {1, 2, 13, 14, 14, 13},
        /* Tri */ {1, 4, 2, 3, 3, 8},
        /* Arch33336 */ {6, 3, 2, 1, 3, 6},
        /* Arch33434 */ {2, 6, 5, 8, 16, 15},
        /* Arch3464 */ {3, 4, 9, 8, 8, 9},
        /* Arch3636 */ {3, 2, 8, 5, 15, 16},
        /* Arch31212 */ {3, 2, 8, 5, 15, 16},
        /* Arch4612 */ {3, 4, 2, 3, 9, 8},
        /* Arch488 */ {1, 2, 5, 7, 7, 10},
        /* Laves3636 */ {3, 2, 8, 5, 15, 16},
        /* Laves31212 */ {3, 4, 9, 8, 8, 9},
        /* Laves4612 */ {2, 12, 1, 2, 4, 6},
        /* Laves488 */ {2, 2, 1, 1, 2, 2},
        /* Laves33434 */ {2, 4, 3, 4, 8, 3},
        /* Laves33336 */ {3, 4, 9, 8, 8, 9},
        /* Laves3464 */ {3, 4, 9, 8, 8, 9},
    };
    const int i = static_cast<int>(t);
    return (i >= static_cast<int>(TilingType::Square) && i < static_cast<int>(kTilingTypeCount))
               ? &kTable[i]
               : nullptr;
}

bool tableInputRestriction(int tilingType, int& ra, int& rb) {
    const TilingType t = static_cast<TilingType>(tilingType);
    const TableDomainParams* prm = tableDomainParams(t);
    if (prm == nullptr || t == TilingType::Square) return false;
    ra = prm->Ra;
    rb = prm->Rb;
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
    const int Ra = prm->Ra, Rb = prm->Rb;
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
