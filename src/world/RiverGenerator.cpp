// RiverGenerator.cpp — 随机成河实现（《河流系统开发文档》§9）。
// 步骤（§9.2）：① 收集山地/陆地格 → ② **河流尝试数 = round(河密度 × 陆地格数)**（§14 决策 D2：
// 密度是"占陆地格"的比例，与城密度同语义）→ ③ 每次尝试：随机源格 → 随机源顶点（临海重试）
// → 沿顶点拓扑游走（softmax 采样）→ 并入全局边集合。时机由 MapGenerator 保证：
// **山地之后、建城之前**（RNG 顺序稳定）。
// **无重边的证明**（§9.4 行 0 与行 2/3 合起来就够，无需任何"已走过的边"的检查）：
//   ① 源顶点若已落在别的河上 → 直接跳过（否则从这里出发必然沿既有河重复走），且按用户要求
//      **不消耗尝试次数**；
//   ② 游走时一旦"到达"任何属于别的河的顶点就立刻终止（行 2/3 的支流交汇），于是每条河只从
//      "尚未有任何河经过的顶点"出发选边；而已记录边的两个端点必然都属于某条河，故
//      **候选边不可能已经是已记录边** ⇒ 输出天然边不相交，末尾的 sort+unique 只是规范化。
#include "world/RiverGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lw {

namespace {

// 共点格数上限：单格最多 12 边（当前最大 12-gon），故共点格 <= 12；留余量。
constexpr int kMaxCellsAtVertex = 16;

// 顶点 V 的共点格（含 cell 自身，去重；只保留界内）。一个格在 V 处恰有 2 条入射边，
// 因此会经两条边各出现一次 —— 靠 add() 去重。
int cellsAtVertex(const TilingGeom& geometry, int cell, int vert, int* outCells) {
    int count = 0;
    const auto add = [&](int candidate) {
        if (candidate < 0 || candidate >= geometry.cellCount()) return;
        for (int i = 0; i < count; ++i)
            if (outCells[i] == candidate) return;
        if (count < kMaxCellsAtVertex) outCells[count++] = candidate;
    };
    add(cell);
    const int edges = geometry.vertexNeighborCount(cell, vert);
    for (int k = 0; k < edges; ++k) {
        int edgeCell = -1, edgeK = -1;
        if (geometry.vertexNeighborEdge(cell, vert, k, edgeCell, edgeK) < 0) continue;
        int a = -1, b = -1;
        geometry.edgeCells(edgeCell, edgeK, a, b);
        add(a);
        add(b);
    }
    return count;
}

// 顶点是否临海：任一**共点格**是海（§4.5/§9.4）。用 R1 的顶点拓扑（不引入浮点比较）。
bool vertexTouchesSea(const TilingGeom& geometry, const std::vector<bool>& land, int cell,
                      int vert) {
    int cells[kMaxCellsAtVertex];
    const int count = cellsAtVertex(geometry, cell, vert, cells);
    for (int i = 0; i < count; ++i)
        if (!land[static_cast<std::size_t>(cells[i])]) return true;
    return false;
}

// 顶点梯度 = 共点格梯度的**面积加权平均**（§9.3）。
GradVec vertexGradient(const TilingGeom& geometry, const std::vector<GradVec>& gradVec, int cell,
                       int vert) {
    int cells[kMaxCellsAtVertex];
    const int count = cellsAtVertex(geometry, cell, vert, cells);
    double sumX = 0.0, sumY = 0.0, sumArea = 0.0;
    for (int i = 0; i < count; ++i) {
        const double area = std::max(1e-9, geometry.cellArea(cells[i]));
        sumX += area * gradVec[static_cast<std::size_t>(cells[i])].x;
        sumY += area * gradVec[static_cast<std::size_t>(cells[i])].y;
        sumArea += area;
    }
    if (sumArea <= 0.0) return {};
    return {sumX / sumArea, sumY / sumArea};
}

// softmax 轮盘赌：**一次** rng.unit()（§9.3）。全部 score 相同时退化为均匀分布。
int sampleSoftmax(const std::vector<double>& scores, double temperature, Rng& rng) {
    double best = scores.front();
    for (double score : scores) best = std::max(best, score);
    const double t = std::max(temperature, 1e-6);
    double total = 0.0;
    for (double score : scores) total += std::exp((score - best) / t);
    double r = rng.unit() * total;
    for (std::size_t i = 0; i < scores.size(); ++i) {
        const double weight = std::exp((scores[i] - best) / t);
        if (r < weight || i + 1 == scores.size()) return static_cast<int>(i);
        r -= weight;
    }
    return static_cast<int>(scores.size()) - 1;
}

struct Candidate {
    std::uint64_t vertex = 0;  // 候选顶点（规范键）
    std::uint64_t edge = 0;    // 连接边（规范键）
    double score = 0.0;
    bool stop = false;  // 候选点临海 → 选中后记边并终止（入海，§9.4 行 3）
};

}  // namespace

std::vector<MapEdgeRef> generateRivers(const TilingGeom& geometry, const std::vector<bool>& land,
                                       const std::vector<bool>& mountain,
                                       const std::vector<GradVec>& gradVec, double riverDensity,
                                       const Config::River::Gen& params, bool gridCoordinates,
                                       Rng& rng, RiverGenStats* stats) {
    std::vector<MapEdgeRef> result;
    const int cellCount = geometry.cellCount();
    if (riverDensity <= 0.0 || cellCount <= 0 || land.size() != static_cast<std::size_t>(cellCount))
        return result;  // 无河路径：不消耗任何 RNG（§9.9 回归红线）

    std::vector<int> mountains;
    std::vector<int> lands;
    for (int i = 0; i < cellCount; ++i) {
        if (!land[static_cast<std::size_t>(i)]) continue;
        lands.push_back(i);
        if (mountain[static_cast<std::size_t>(i)]) mountains.push_back(i);
    }
    if (lands.empty()) return result;

    const auto vertexPosition = [&](int cell, int vert, double& x, double& y) {
        if (gridCoordinates)
            geometry.gridVertex(cell, vert, x, y);
        else
            geometry.cellVertex(cell, vert, x, y);
    };

    // 全局已记录边的端点顶点（升序；"其它河流上的点" → 选中后终止 = 支流交汇）。
    std::vector<std::uint64_t> otherRiverVerts;
    std::vector<std::uint64_t> riverEdges;
    std::vector<std::uint64_t> riverVerts;
    std::vector<std::uint64_t> visited;
    std::vector<Candidate> candidates;
    std::vector<double> scores;
    std::vector<std::uint64_t> allKeys;

    const auto traceRiver = [&](std::uint64_t startKey) {
        riverEdges.clear();
        riverVerts.clear();
        visited.clear();
        visited.push_back(startKey);
        std::uint64_t current = startKey;
        GradVec previousDirection{0.0, 0.0};
        bool first = true;
        for (int step = 0; params.maxStepsPerRiver <= 0 || step < params.maxStepsPerRiver;
             ++step) {
            int cell = -1, vert = -1;
            if (!geometry.vertexFromKey(current, cell, vert)) break;
            GradVec gradient = vertexGradient(geometry, gradVec, cell, vert);
            if (params.flowDownhill) {  // 顺坡 = 沿 -∇h
                gradient.x = -gradient.x;
                gradient.y = -gradient.y;
            }
            double cx = 0.0, cy = 0.0;
            vertexPosition(cell, vert, cx, cy);
            candidates.clear();
            scores.clear();
            const int neighbors = geometry.vertexNeighborCount(cell, vert);
            for (int k = 0; k < neighbors; ++k) {
                int nextCell = -1, nextVert = -1;
                if (geometry.vertexNeighbor(cell, vert, k, nextCell, nextVert) < 0) continue;
                const std::uint64_t nextKey = geometry.vertexNeighborKey(cell, vert, k);
                if (nextKey == 0) continue;
                // §9.4 行 1：本次河流已访问的点（含上一点/绕圈）不作候选。
                if (std::find(visited.begin(), visited.end(), nextKey) != visited.end()) continue;
                int edgeCell = -1, edgeK = -1;
                if (geometry.vertexNeighborEdge(cell, vert, k, edgeCell, edgeK) < 0) continue;
                int a = -1, b = -1;
                geometry.edgeCells(edgeCell, edgeK, a, b);
                // §9.4 行 6（R5 严格口径）：临海/地图边界边不可走。
                if (b < 0 || !land[static_cast<std::size_t>(a)] ||
                    !land[static_cast<std::size_t>(b)])
                    continue;
                const std::uint64_t edgeKey = geometry.edgeKey(edgeCell, edgeK);
                double nx = 0.0, ny = 0.0;
                vertexPosition(nextCell, nextVert, nx, ny);
                const double dx = nx - cx, dy = ny - cy;
                const double length = std::hypot(dx, dy);
                if (length <= 1e-12) continue;
                const double ux = dx / length, uy = dy / length;  // 只取方向（§9.3）
                const bool candidateSea = vertexTouchesSea(geometry, land, nextCell, nextVert);
                double score = params.gradientWeight * (gradient.x * ux + gradient.y * uy);
                if (!first)
                    score += params.straightnessWeight
                             * (previousDirection.x * ux + previousDirection.y * uy);
                if (candidateSea) score += params.mouthWeight;
                candidates.push_back({nextKey, edgeKey, score, candidateSea});
                scores.push_back(score);
            }
            if (candidates.empty()) break;  // §9.4 行 4：内陆河终止
            const Candidate& chosen = candidates[static_cast<std::size_t>(
                sampleSoftmax(scores, params.temperature, rng))];
            riverEdges.push_back(chosen.edge);
            const bool onOtherRiver =
                std::binary_search(otherRiverVerts.begin(), otherRiverVerts.end(), chosen.vertex);
            if (chosen.stop || onOtherRiver) break;  // §9.4 行 2/3：入海 / 支流交汇
            visited.push_back(chosen.vertex);
            int nextCell = -1, nextVert = -1;
            if (!geometry.vertexFromKey(chosen.vertex, nextCell, nextVert)) break;
            double nx = 0.0, ny = 0.0;
            vertexPosition(nextCell, nextVert, nx, ny);
            const double dx = nx - cx, dy = ny - cy;
            const double length = std::hypot(dx, dy);
            if (length > 1e-12) previousDirection = {dx / length, dy / length};
            first = false;
            current = chosen.vertex;
        }
        // 本河端点顶点并入全局集合（供后续河判定支流交汇）。
        for (const std::uint64_t key : riverEdges) {
            int cell = -1, k = -1, vA = -1, vB = -1;
            if (!geometry.edgeFromKey(key, cell, k) ||
                !geometry.cellEdgeVertices(cell, k, vA, vB))
                continue;
            const std::uint64_t p = geometry.vertexKey(cell, vA);
            const std::uint64_t q = geometry.vertexKey(cell, vB);
            if (p != 0) riverVerts.push_back(p);
            if (q != 0) riverVerts.push_back(q);
        }
    };

    // §14 决策 D2：河密度 = **占陆地格**比（与城密度同语义）——尝试数按陆地格数算，
    // 而不是总格数（含海，会让实际河数随海占比漂移）。
    const int riverCount = static_cast<int>(std::llround(riverDensity * lands.size()));
    if (stats) stats->planned = riverCount;
    for (int river = 0; river < riverCount; ++river) {
        const std::vector<int>& pool = mountains.empty() ? lands : mountains;
        bool traced = false;
        int attempt = 0;
        // "源顶点已在别的河上"的跳过不消耗尝试次数（§9.4 行 0），但要有界：最多白跳
        // (sourceRetryPerRiver + 1) 次后就当普通失败处理，避免河网很密时空转到死。
        int freeSkips = 0;
        const int maxFreeSkips = params.sourceRetryPerRiver + 1;
        while (!traced && attempt <= params.sourceRetryPerRiver && freeSkips < maxFreeSkips) {
            const int source =
                pool[static_cast<std::size_t>(rng.get(static_cast<int>(pool.size()) - 1))];
            const int vertexCount = geometry.cellVertexCount(source);
            if (vertexCount <= 0) {
                ++attempt;
                continue;
            }
            const int vert = rng.get(vertexCount - 1);
            if (vertexTouchesSea(geometry, land, source, vert)) {  // 源顶点临海 → 重试
                ++attempt;
                continue;
            }
            const std::uint64_t startKey = geometry.vertexKey(source, vert);
            if (startKey == 0) {
                ++attempt;
                continue;
            }
            // §9.4 行 0（R7 修正）：源顶点已落在别的河上 → 从这里出发必然沿既有河重复走边，
            // 直接换源且**不消耗尝试次数**（此前会白耗一次尝试并制造重边）。
            if (std::binary_search(otherRiverVerts.begin(), otherRiverVerts.end(), startKey)) {
                ++freeSkips;
                if (stats) ++stats->sourceSkips;
                continue;
            }
            traceRiver(startKey);
            if (riverEdges.empty()) {  // 本次尝试作废（换源重试）
                ++attempt;
                continue;
            }
            traced = true;
            if (stats) {
                ++stats->rivers;
                stats->traversals += static_cast<int>(riverEdges.size());
            }
            allKeys.insert(allKeys.end(), riverEdges.begin(), riverEdges.end());
            otherRiverVerts.insert(otherRiverVerts.end(), riverVerts.begin(), riverVerts.end());
            std::sort(otherRiverVerts.begin(), otherRiverVerts.end());
            otherRiverVerts.erase(std::unique(otherRiverVerts.begin(), otherRiverVerts.end()),
                                  otherRiverVerts.end());
        }
    }

    std::sort(allKeys.begin(), allKeys.end());
    allKeys.erase(std::unique(allKeys.begin(), allKeys.end()), allKeys.end());
    result.reserve(allKeys.size());
    for (const std::uint64_t key : allKeys) {
        int cell = -1, edge = -1;
        if (geometry.edgeFromKey(key, cell, edge)) result.push_back({cell, edge});
    }
    return result;
}

}  // namespace lw
