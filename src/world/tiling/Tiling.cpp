// Tiling.cpp — 密铺几何实现（P12 异种地图）。纯几何，RNG 0 次。
// 坐标/邻接/穿越语义见 Tiling.h 头注与开发计划 P12 §1-§2。
#include "world/tiling/Tiling.h"

#include <algorithm>
#include <array>
#include <fstream>
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
// ---------------------------------------------------------------------------
struct TilingTable {
    double wx = 0.0, hx = 0.0, hy = 0.0;  // W=(wx,0)、H=(hx,hy)
    double rx = 0.0, ry = 0.0;            // 基础格多边形相对中心的 AABB 半径（保守）
    struct Edge {
        int nb = -1;   // 邻格基础格下标
        int dr = 0;    // 邻格所在行偏移
        int dc = 0;    // 邻格所在列偏移
    };
    struct Cell {
        int n = 0;
        double cx = 0.0, cy = 0.0;
        std::vector<std::array<double, 2>> v;  // 逆时针顶点（与边序一致）
        std::vector<Edge> edges;               // 与 v[i]→v[i+1] 同序
    };
    std::vector<Cell> cells;
};

namespace {

constexpr double kTableTol = 1e-6;

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

int pointInConvex(const std::vector<std::array<double, 2>>& poly, double x, double y) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) return 0;
    int sign = 0;
    for (int i = 0; i < n; ++i) {
        const auto& a = poly[static_cast<size_t>(i)];
        const auto& b = poly[static_cast<size_t>((i + 1) % n)];
        const double cross = (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
        if (std::fabs(cross) <= kTableTol) continue;
        const int s = cross > 0.0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (sign != s) return 0;
    }
    return 1;
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
    // 保守 AABB 半径（相对格中心）。
    for (const auto& c : tab->cells) {
        for (const auto& p : c.v) {
            tab->rx = std::max(tab->rx, std::fabs(p[0] - c.cx));
            tab->ry = std::max(tab->ry, std::fabs(p[1] - c.cy));
        }
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
                    const double oy = static_cast<double>(dr) * tab->hy;
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
    return tab;
}

}  // namespace

void TilingGeom::ensureTable() const {
    if (!isTableType(type) || table_) return;
    table_ = loadTable(type);
}

int TilingGeom::cellCount() const {
    switch (type) {
        case TilingType::Square: return cols * rows;
        case TilingType::Hex: return cols * rows;
        case TilingType::Tri: return 2 * cols * rows;
        default: {
            ensureTable();
            if (!table_) return 0;
            return static_cast<int>(table_->cells.size()) * cols * rows;
        }
    }
}

double TilingGeom::worldWidth() const {
    switch (type) {
        case TilingType::Square: return static_cast<double>(cols);
        case TilingType::Hex: return kHexColSpacing * static_cast<double>(cols);
        case TilingType::Tri: return kTriSide * static_cast<double>(cols);
        default: {
            ensureTable();
            return table_ ? table_->wx * static_cast<double>(cols) : 0.0;
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
            return table_ ? table_->hy * static_cast<double>(rows) : 0.0;
        }
    }
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
    switch (type) {
        case TilingType::Square:
        case TilingType::Hex: return r * cols + c;
        case TilingType::Tri: return 2 * (r * cols + c) + (b & 1);
        default: {
            ensureTable();
            if (!table_) return -1;
            const int B = static_cast<int>(table_->cells.size());
            if (r < 0 || r >= rows || c < 0 || c >= cols || b < 0 || b >= B) return -1;
            return (r * cols + c) * B + b;
        }
    }
}

void TilingGeom::indexToRowCol(int index, int& r, int& c, int& b) const {
    switch (type) {
        case TilingType::Square:
        case TilingType::Hex:
            r = index / cols;
            c = index % cols;
            b = 0;
            return;
        case TilingType::Tri:
            r = (index >> 1) / cols;
            c = (index >> 1) % cols;
            b = index & 1;
            return;
        default:
            ensureTable();
            if (!table_) {
                r = c = b = -1;
                return;
            }
            b = index % static_cast<int>(table_->cells.size());
            const int rc = index / static_cast<int>(table_->cells.size());
            r = rc / cols;
            c = rc % cols;
            return;
    }
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
            wx = cell.cx + static_cast<double>(c) * table_->wx + static_cast<double>(r) * table_->hx;
            wy = cell.cy + static_cast<double>(r) * table_->hy;
            return;
        }
    }
    wx = wy = 0.0;
}

int TilingGeom::cellPolygon(int index, double* wx, double* wy, int maxVerts) const {
    double cx, cy;
    cellCenter(index, cx, cy);
    switch (type) {
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
            for (int i = 0; i < cell.n; ++i) {
                wx[i] = cell.v[static_cast<size_t>(i)][0] + static_cast<double>(c) * table_->wx
                        + static_cast<double>(r) * table_->hx;
                wy[i] = cell.v[static_cast<size_t>(i)][1] + static_cast<double>(r) * table_->hy;
            }
            return cell.n;
        }
    }
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
            const int c0 = static_cast<int>(std::lround(wx / kHexColSpacing - 0.5 * (r & 1)));
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
            if (wx < -table_->rx || wx > worldWidth() + table_->rx || wy < -table_->ry
                || wy > worldHeight() + table_->ry)
                return -1;
            const int B = static_cast<int>(table_->cells.size());
            // 对每个基础格 b 解 p - center_b = c*W + r*H，取最近整数行列，再 ±1 邻域
            // 验证点是否落在平移后的基础多边形内。扫描序固定：b 升序，r 邻域 -1..1，
            // c 邻域 -1..1（含候选 c0,c0±1）。
            for (int b = 0; b < B; ++b) {
                const auto& cell = table_->cells[static_cast<size_t>(b)];
                const double px = wx - cell.cx, py = wy - cell.cy;
                const double c0f = px / table_->wx - (table_->hx / table_->wx) * (py / table_->hy);
                const double r0f = py / table_->hy;
                const int c0 = static_cast<int>(std::lround(c0f));
                const int r0 = static_cast<int>(std::lround(r0f));
                for (int dr = -1; dr <= 1; ++dr) {
                    const int r = r0 + dr;
                    if (r < 0 || r >= rows) continue;
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int c = c0 + dc;
                        if (c < 0 || c >= cols) continue;
                        const double ox = static_cast<double>(c) * table_->wx + static_cast<double>(r) * table_->hx;
                        const double oy = static_cast<double>(r) * table_->hy;
                        std::vector<std::array<double, 2>> poly;
                        poly.reserve(cell.v.size());
                        for (const auto& p : cell.v)
                            poly.push_back({p[0] + ox, p[1] + oy});
                        if (pointInConvex(poly, wx, wy))
                            return (r * cols + c) * B + b;
                    }
                }
            }
            return -1;
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
                r0 = static_cast<int>(std::floor((y0 - table_->ry) / table_->hy)) - 1;
                r1 = static_cast<int>(std::ceil((y1 + table_->ry) / table_->hy)) + 1;
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
                const double baseX = static_cast<double>(r) * table_->hx;
                c0 = static_cast<int>(std::floor((x0 - baseX - table_->rx) / table_->wx)) - 1;
                c1 = static_cast<int>(std::ceil((x1 - baseX + table_->rx) / table_->wx)) + 1;
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

}  // namespace lw
