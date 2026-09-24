// MapGenerator.cpp — 随机地图生成实现（开发计划 P6）。
// 算法（确定性）：① Rng(seed) 洗牌生成 256 值噪声查表 → ② fBm 海拔场 h∈[0,1] →
// ③ 排序取 seaRatio 分位数切海陆（可选强制边缘为海）→ ④ 山 = 高原分量（按基础海拔取前段，
// 大块高地）+ 山脉分量（按独立种子山脊噪声取前段，蜿蜒山脊）→ ⑤ 城概率按地形权重 →
// ⑥ 输出 fully-resolved terrain and city records.
#include "world/MapGenerator.h"

#include "world/RiverGenerator.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include "core/Random.h"
#include "core/Paths.h"
#include "world/Map.h"
#include "world/tiling/Tiling.h"

namespace lw {

namespace {

// 值噪声（lattice 随机值 + 双线性 + smoothstep）。确定性：perm 由 Rng(seed) 洗牌生成。
class ValueNoise2D {
public:
    explicit ValueNoise2D(Rng& rng) {
        std::array<unsigned char, 256> p{};
        for (int i = 0; i < 256; ++i) p[static_cast<size_t>(i)] = static_cast<unsigned char>(i);
        for (int i = 255; i > 0; --i) {  // Fisher-Yates（用注入 rng，确定性）
            const int j = rng.get(i);    // [0, i]
            std::swap(p[static_cast<size_t>(i)], p[static_cast<size_t>(j)]);
        }
        perm_ = p;
    }

    // 格点随机值 ∈ [0,1)（坐标哈希两次查表）。
    double hash(int ix, int iy) const {
        const unsigned a = perm_[static_cast<unsigned>(ix) & 255u];
        const unsigned b = perm_[(a + static_cast<unsigned>(iy)) & 255u];
        return static_cast<double>(b) / 255.0;
    }

    // fBm：3 octave（lacunarity 2、gain 0.5），归一化到 [0,1]。
    double fbm(double x, double y) const {
        double sum = 0.0, amp = 1.0, norm = 0.0;
        for (int o = 0; o < 3; ++o) {
            sum += amp * at(x, y);
            norm += amp;
            x *= 2.0;
            y *= 2.0;
            amp *= 0.5;
        }
        return sum / norm;
    }

    // 山脊噪声（ridged multifractal，2026-08-06 用户要求"山脉"形状）：每 octave 取
    // 1-|2n-1| 再平方 → 在中点 n=0.5 处形成锐利"脊线"，fBm 叠加 → 蜿蜒山脉，非平滑块状。
    double ridgeFbm(double x, double y) const {
        double sum = 0.0, amp = 1.0, norm = 0.0;
        for (int o = 0; o < 3; ++o) {
            const double n = at(x, y);
            double r = 1.0 - std::abs(2.0 * n - 1.0);
            r *= r;
            sum += amp * r;
            norm += amp;
            x *= 2.0;
            y *= 2.0;
            amp *= 0.5;
        }
        return sum / norm;
    }

    // 单 octave 值噪声采样（供外部拼单 octave 场，如 crease 脊线场）。
    double at(double x, double y) const {
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const double fx = fade(x - x0);
        const double fy = fade(y - y0);
        const double v00 = hash(x0, y0), v10 = hash(x0 + 1, y0);
        const double v01 = hash(x0, y0 + 1), v11 = hash(x0 + 1, y0 + 1);
        const double a = v00 + fx * (v10 - v00);
        const double b = v01 + fx * (v11 - v01);
        return a + fy * (b - a);
    }

private:
    static double fade(double t) { return t * t * (3.0 - 2.0 * t); }

    std::array<unsigned char, 256> perm_{};
};

// 用邻格中心的二维位移拟合局部平面，返回单位距离上的海拔变化。
// 直接取邻格差值最大值会偏向邻居更多或中心距更大的大多边形。
// 返回值：vector = 拟合梯度向量（det≈0 退化时 {0,0}）；magnitude = 原标量（含 maxSlope 回退），
// 山分布/语义基线逐位不变（§9.5）。
struct GradientFit {
    GradVec vector;
    double magnitude = 0.0;
};

GradientFit estimateGradientVector(const TilingGeom& g, int index,
                                   const std::vector<double>& height, bool gridCoordinates) {
    auto position = [&](int idx, double& x, double& y) {
        if (gridCoordinates)
            g.gridCenter(idx, x, y);
        else
            g.cellCenter(idx, x, y);
    };

    double x0, y0;
    position(index, x0, y0);
    const double h0 = height[static_cast<size_t>(index)];
    double xx = 0.0, xy = 0.0, yy = 0.0;
    double bx = 0.0, by = 0.0, maxSlope = 0.0;
    const int count = g.type == TilingType::Square ? g.pointNeighborCount(index)
                                                    : g.neighborCount(index);
    for (int k = 0; k < count; ++k) {
        const int nb = g.type == TilingType::Square ? g.pointNeighbor(index, k)
                                                     : g.neighbor(index, k);
        if (nb < 0) continue;
        double x1, y1;
        position(nb, x1, y1);
        const double dx = x1 - x0, dy = y1 - y0;
        const double d2 = dx * dx + dy * dy;
        if (d2 <= 1e-12) continue;
        const double dh = height[static_cast<size_t>(nb)] - h0;
        xx += dx * dx;
        xy += dx * dy;
        yy += dy * dy;
        bx += dx * dh;
        by += dy * dh;
        maxSlope = std::max(maxSlope, std::fabs(dh) / std::sqrt(d2));
    }
    const double det = xx * yy - xy * xy;
    if (det <= 1e-12) return {GradVec{0.0, 0.0}, maxSlope};
    const double gx = (bx * yy - by * xy) / det;
    const double gy = (by * xx - bx * xy) / det;
    return {GradVec{gx, gy}, std::hypot(gx, gy)};
}

bool hasOutsidePointNeighbor(const TilingGeom& g, int index) {
    for (int k = 0; k < g.pointNeighborCount(index); ++k)
        if (g.pointNeighbor(index, k) < 0) return true;
    return false;
}

// 强制海岸的第二部分：降低靠近真实地图边界的海拔，使海岸从边缘向内自然形成。
void applyCoastElevationFalloff(std::vector<double>& height, bool forceCoast,
                                const TilingGeom& geometry, double rangeMultiplier,
                                double strengthMultiplier) {
    constexpr double kBaseEdgeBand = 3.0;
    constexpr double kBaseEdgeStrength = 0.5;
    const double edgeBand = kBaseEdgeBand * rangeMultiplier;
    const double edgeStrength = kBaseEdgeStrength * strengthMultiplier;
    if (!forceCoast || edgeBand <= 0.0 || edgeStrength <= 0.0) return;

    for (size_t index = 0; index < height.size(); ++index) {
        const double distance = geometry.cellBoundaryDistance(static_cast<int>(index));
        if (distance >= edgeBand) continue;

        const double u = 1.0 - distance / edgeBand;
        const double smooth = u * u * (3.0 - 2.0 * u);
        height[index] = std::max(0.0, height[index] - edgeStrength * smooth);
    }
}

}  // namespace

// 前向声明（generate 分派用；实现见文件后部）。
static bool generateSquare(MapDefinition& out, std::uint32_t seed, const MapGenParams& p,
                           const Config::City& cityConfig, const Config::River::Gen& riverGen);
static bool generateTiled(MapDefinition& out, std::uint32_t seed, const MapGenParams& p,
                          const Config::City& cityConfig, const Config::River::Gen& riverGen);

static bool finishDefinition(MapDefinition& out, const MapGenParams& p,
                             const Config::City& cityConfig, const std::vector<bool>& land,
                             const std::vector<bool>& mountain,
                             const std::vector<MapEdgeRef>& rivers, Rng& rng) {
    out.cols = p.width;
    out.rows = p.height;
    out.tiling = p.tiling;
    out.terrain.resize(land.size());
    for (size_t i = 0; i < land.size(); ++i)
        out.terrain[i] = !land[i] ? MapTerrain::Sea
                         : mountain[i] ? MapTerrain::Mountain
                                       : MapTerrain::Land;

    Map map;
    map.configureCanonical(p.tiling, p.width, p.height);
    map.setCityConfig(cityConfig);
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        map.atIndex(idx).land = land[static_cast<size_t>(idx)];
        map.atIndex(idx).mountain = mountain[static_cast<size_t>(idx)];
        map.atIndex(idx).cityAllowed = map.atIndex(idx).land;
    }
    out.rivers = rivers;  // 河（§9.2）：只序列化边，顶点由边导出
    map.populateRandomCities(rng, p.cityDensity, p.cityMountainWeight);
    out.cities.reserve(map.cityCount());
    for (const City& city : map.cities()) {
        out.cities.push_back({city.level, city.baseIndex, city.shapeVariant});
        for (const int index : map.cityCells(city))
            if (index >= 0) out.terrain[static_cast<std::size_t>(index)] = MapTerrain::City;
    }
    return out.validate();
}

static bool finishGeneratedTerrain(MapDefinition& out, const MapGenParams& p,
                                   const Config::City& cityConfig,
                                   const Config::River::Gen& riverGen, const TilingGeom& geometry,
                                   std::vector<double>& height, bool gridCoordinates, Rng& rng) {
    applyCoastElevationFalloff(height, p.forceCoast, geometry, p.forceCoastRangeMultiplier,
                               p.forceCoastStrengthMultiplier);

    std::vector<double> grad(height.size(), 0.0);
    std::vector<GradVec> gradVec(height.size());
    for (int index = 0; index < geometry.cellCount(); ++index) {
        const GradientFit fit = estimateGradientVector(geometry, index, height, gridCoordinates);
        grad[static_cast<size_t>(index)] = fit.magnitude;  // 与旧 estimateGradient 逐位一致
        gradVec[static_cast<size_t>(index)] = fit.vector;
    }

    std::vector<double> sorted = height;
    std::sort(sorted.begin(), sorted.end());
    const size_t thresholdIndex =
        std::min(static_cast<size_t>(static_cast<double>(sorted.size()) * p.seaRatio),
                 sorted.size() - 1);
    const double threshold = sorted[thresholdIndex];
    std::vector<bool> land(height.size(), false);
    for (size_t i = 0; i < land.size(); ++i) land[i] = height[i] >= threshold;
    if (p.forceCoast) {
        for (int index = 0; index < geometry.cellCount(); ++index)
            if (hasOutsidePointNeighbor(geometry, index)) land[static_cast<size_t>(index)] = false;
    }

    std::vector<size_t> orderH, orderG;
    for (int index = 0; index < geometry.cellCount(); ++index) {
        if (!land[static_cast<size_t>(index)]) continue;
        bool adjacentSea = false;
        for (int k = 0; k < geometry.pointNeighborCount(index) && !adjacentSea; ++k) {
            const int neighbor = geometry.pointNeighbor(index, k);
            adjacentSea = neighbor >= 0 && !land[static_cast<size_t>(neighbor)];
        }
        if (!adjacentSea) {
            orderH.push_back(static_cast<size_t>(index));
            orderG.push_back(static_cast<size_t>(index));
        }
    }
    std::sort(orderH.begin(), orderH.end(), [&](size_t a, size_t b) {
        if (height[a] != height[b]) return height[a] > height[b];
        return a < b;
    });
    std::sort(orderG.begin(), orderG.end(), [&](size_t a, size_t b) {
        if (grad[a] != grad[b]) return grad[a] > grad[b];
        return a < b;
    });

    std::vector<bool> isMountain(land.size(), false);
    constexpr double kPlateauShare = 0.4;
    const size_t mountainCount =
        static_cast<size_t>(std::llround(p.mountainDensity * orderH.size()));
    const size_t plateauCount =
        static_cast<size_t>(std::llround(mountainCount * kPlateauShare));
    const size_t rangeCount = mountainCount - plateauCount;
    for (size_t i = 0; i < plateauCount && i < orderH.size(); ++i)
        isMountain[orderH[i]] = true;
    for (size_t i = 0; i < rangeCount && i < orderG.size(); ++i)
        isMountain[orderG[i]] = true;
    // 河流系统 §9.2（R6）：**山地之后、建城之前** → RNG 顺序 = 海拔/山 → 河 → 城。
    // 密度 0 时不进入生成器、不消耗 RNG（旧输出逐字节不变，§9.9）。
    const std::vector<MapEdgeRef> rivers =
        generateRivers(geometry, land, isMountain, gradVec, p.riverDensity, riverGen,
                       gridCoordinates, rng);
    return finishDefinition(out, p, cityConfig, land, isMountain, rivers, rng);
}

MapGenParams normalizedParams(const MapGenParams& raw) {
    MapGenParams p = raw;
    p.width = std::clamp(p.width, 32, 200);
    p.height = std::clamp(p.height, 32, 200);
    p.seaRatio = std::clamp(p.seaRatio, 0.0, 0.90);
    p.mountainDensity = std::clamp(p.mountainDensity, 0.0, 0.9);
    p.cityDensity = std::clamp(p.cityDensity, 0.0, 0.9);
    p.riverDensity = std::clamp(p.riverDensity, 0.0, 0.9);
    const auto normalizeMultiplier = [](double value) {
        return std::isfinite(value) && value >= 0.0 ? value : 1.0;
    };
    p.forceCoastRangeMultiplier = normalizeMultiplier(p.forceCoastRangeMultiplier);
    p.forceCoastStrengthMultiplier = normalizeMultiplier(p.forceCoastStrengthMultiplier);
    int cols = p.width, rows = p.height;
    chooseTableDomain(static_cast<int>(p.tiling), p.width, p.height, cols, rows);
    p.width = cols;
    p.height = rows;
    return p;
}

std::string MapGenerator::defaultPath(std::uint32_t seed, const MapGenParams& p,
                                      const Config::River::Gen& /*riverGen*/) {
    const MapGenParams n = normalizedParams(p);
    std::ostringstream key;
    key << kMapDataDir << "/gen_" << seed << "_" << tilingName(n.tiling) << "_"
        << n.width << "x" << n.height << "_sea" << std::fixed << std::setprecision(9)
         << n.seaRatio << "_mtn" << n.mountainDensity << "_city" << n.cityDensity
         << "_river" << n.riverDensity
         << "_coast" << (n.forceCoast ? 1 : 0)
         << "_coastRange" << n.forceCoastRangeMultiplier
         << "_coastStrength" << n.forceCoastStrengthMultiplier
          << ".landmap";
    return key.str();
}

bool MapGenerator::generate(std::uint32_t seed, const MapGenParams& raw, MapDefinition& out,
                            const Config::City& cityConfig, const Config::River::Gen& riverGen) {
    const MapGenParams p = normalizedParams(raw);
    out = MapDefinition{};
    if (p.tiling == TilingType::Square)
        return generateSquare(out, seed, p, cityConfig, riverGen);
    return generateTiled(out, seed, p, cityConfig, riverGen);
}

bool MapGenerator::generate(const std::string& path, std::uint32_t seed, const MapGenParams& raw) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::string dir =
        (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
    if (!ensureDirExists(dir)) {
        spdlog::error("map generate: cannot create dir '{}'", dir);
        return false;
    }
    MapDefinition definition;
    if (!generate(seed, raw, definition)) return false;
    std::string error;
    if (!definition.saveToFile(path, &error)) {
        spdlog::error("MapGenerator: '{}' write failed: {}", path, error);
        return false;
    }
    return true;
}

// Square tiling terrain generation.
static bool generateSquare(MapDefinition& out, std::uint32_t seed, const MapGenParams& p,
                           const Config::City& cityConfig, const Config::River::Gen& riverGen) {
    const int w = p.width, h = p.height;
    Rng rng(seed);
    ValueNoise2D noise(rng);
    const double baseCell = std::max(w, h) / 6.0;

    // ① 海拔场。
    std::vector<double> height(static_cast<size_t>(w) * h, 0.0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            height[static_cast<size_t>(y) * w + x] = noise.fbm(x / baseCell, y / baseCell);
        }
    }
    const TilingGeom squareGeom{TilingType::Square, w, h};
    return finishGeneratedTerrain(out, p, cityConfig, riverGen, squareGeom, height, false, rng);
}

// 六/三角密铺：海拔场每格中心**直接采样 fbm**（最朴素原始版，无任何平滑/平均/插值
// 后处理；2026-08-15 用户拍板回退，先以纯净基线定位"横纹/同向三角"现象），其余流程与
// 方形一致（分位数切海陆 + 内陆山 + 城权重），输出 native terrain records。
static bool generateTiled(MapDefinition& out, std::uint32_t seed, const MapGenParams& p,
                          const Config::City& cityConfig, const Config::River::Gen& riverGen) {
    const TilingGeom g{p.tiling, p.width, p.height};
    const int cellCount = g.cellCount();
    Rng rng(seed);
    ValueNoise2D noise(rng);
    // 斜周期在周期基坐标中采样，避免世界坐标剪切噪声；gridCenter 保留块内基础格位置。
    const bool skew = g.hasSkewedPeriod();
    const double baseCell = skew
                                ? static_cast<double>(std::max(g.cols, g.rows)) / 6.0
                                : std::max(g.worldWidth(), g.worldHeight()) / 6.0;

    // ① 海拔场（skew：周期坐标中的格中心；否则：世界坐标中的格中心）。
    std::vector<double> height(static_cast<size_t>(cellCount), 0.0);
    for (int idx = 0; idx < cellCount; ++idx) {
        double gx, gy;
        if (skew)
            g.gridCenter(idx, gx, gy);
        else
            g.cellCenter(idx, gx, gy);
        height[static_cast<size_t>(idx)] = noise.fbm(gx / baseCell, gy / baseCell);
    }
    return finishGeneratedTerrain(out, p, cityConfig, riverGen, g, height, skew, rng);
}

}  // namespace lw
