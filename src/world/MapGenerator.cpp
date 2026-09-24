// MapGenerator.cpp — 随机地图生成实现（开发计划 P6）。
// 算法（确定性）：① Rng(seed) 洗牌生成 256 值噪声查表 → ② fBm 海拔场 h∈[0,1]（2 octave，
//   2026-09-24 由 3 降为 2 —— 见 ValueNoise2D::fbm 的说明）→ ③ 梯度由**噪声场**中心差分解析
//   给出（与格型/格面积无关，见 ValueNoise2D::gradient）→ ④ 可选强制海岸：按**格心**到边界
//   距离衰减海拔 → ⑤ 排序取 seaRatio 分位切海陆（**格数**分位，滑条语义）→ ⑥ 山 = 高原分量
//   （陆地内陆格按海拔取前 40%）+ 山脉分量（同集合内按 |∇h| 取余下 60%）→ ⑦ 河（R6）→
//   ⑧ 城概率按地形权重 → ⑨ 输出 fully-resolved terrain and city records。
// 注意：① 海拔场、分位阈值、梯度、选山**均无后处理**（历史上的"消 1 格碎格"已删除，
// 详见 .docs/old/2026_08_开发计划.md §0）；② **每个 RNG 阶段用独立子流**（见 MapGenStage）：
// `Rng::deriveSeed(seed, salt)` 派生，阶段之间不再共用一条流。
#include "world/MapGenerator.h"

#include "world/RiverGenerator.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

#include "core/Random.h"
#include "core/Paths.h"
#include "world/Map.h"
#include "world/tiling/Tiling.h"

namespace lw {

namespace {

// 分阶段 RNG 盐（用户 2026-09-24 决策）：同 seed 下"噪声表 / 河 / 城"各用一条独立子流
// （`Rng::deriveSeed(seed, salt)`），于是插入或调换阶段不会挪动其它阶段的随机数；
// 同一阶段内仍严格由 (seed, salt) 唯一决定。
constexpr std::uint32_t kStageNoise = 0x0001u;
constexpr std::uint32_t kStageRiver = 0x0002u;
constexpr std::uint32_t kStageCity = 0x0003u;

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

    // fBm：2 octave（lacunarity 2、gain 0.5），归一化到 [0,1]。
    // 2026-09-24 修"异种密铺碎格"：3 octave 时最细波长 = baseCell/4 ≈ 5 U，与异种密铺
    // 最大格（1.9~3.5 U）同量级 —— 阈值等值线曲率半径 λ/2π ≈ 0.83 U ≈ 最小格尺寸，
    // 海岸线能在单格内绕出小圈（实测 arch_31212 孤岛 19 个）；改为 2 octave 后最细波长
    // = baseCell/2 ≈ 10 U（≈3~5 倍最大格），配合"梯度取自场"与"边缘衰减取自位置"，
    // 实测孤岛 19→1、孤山 138→28（全 17 密铺 × 多种子）。
    double fbm(double x, double y) const {
        double sum = 0.0, amp = 1.0, norm = 0.0;
        for (int o = 0; o < 2; ++o) {
            sum += amp * at(x, y);
            norm += amp;
            x *= 2.0;
            y *= 2.0;
            amp *= 0.5;
        }
        return sum / norm;
    }

    // 解析梯度（固定步长中心差分）：与网格无关，故不受格型/格面积影响。
    // 步长取 baseCell/64 ≪ 最细 octave 的格点间距（baseCell/4），无混叠。
    void gradient(double x, double y, double h, double& gx, double& gy) const {
        gx = (fbm(x + h, y) - fbm(x - h, y)) / (2.0 * h);
        gy = (fbm(x, y + h) - fbm(x, y - h)) / (2.0 * h);
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
        const double distance = geometry.centerBoundaryDistance(static_cast<int>(index));
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
                             const std::vector<MapEdgeRef>& rivers, Rng& cityRng) {
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
    map.populateRandomCities(cityRng, p.cityDensity, p.cityMountainWeight);
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
                                   std::vector<double>& height,
                                   const std::vector<GradVec>& gradVec, bool gridCoordinates,
                                   Rng& riverRng, Rng& cityRng) {
    applyCoastElevationFalloff(height, p.forceCoast, geometry, p.forceCoastRangeMultiplier,
                               p.forceCoastStrengthMultiplier);

    // 坡度标量（山脉"range"分量排序用）：直接取解析梯度的模长。
    std::vector<double> grad(height.size(), 0.0);
    for (int index = 0; index < geometry.cellCount(); ++index)
        grad[static_cast<size_t>(index)] =
            std::hypot(gradVec[static_cast<size_t>(index)].x,
                       gradVec[static_cast<size_t>(index)].y);

    // ② 海/陆阈值：升序分位（seaRatio = 海占**格数**比，滑条语义不变）。
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
    // 河流系统 §9.2（R6）：**山地之后、建城之前**；各阶段用**独立子流**（kStageRiver /
    // kStageCity），故河流阶段的存在/参数变化都不再挪动城市阶段的随机数。
    // 密度 0 时不进入生成器、不消耗任何 RNG（§9.9）。
    const std::vector<MapEdgeRef> rivers =
        generateRivers(geometry, land, isMountain, gradVec, p.riverDensity, riverGen,
                       gridCoordinates, riverRng);
    return finishDefinition(out, p, cityConfig, land, isMountain, rivers, cityRng);
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

bool MapGenerator::generate(const std::string& path, std::uint32_t seed, const MapGenParams& raw,
                            const Config::City& cityConfig,
                            const Config::River::Gen& riverGen) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::string dir =
        (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
    if (!ensureDirExists(dir)) {
        spdlog::error("map generate: cannot create dir '{}'", dir);
        return false;
    }
    MapDefinition definition;
    if (!generate(seed, raw, definition, cityConfig, riverGen)) return false;
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
    Rng noiseRng(Rng::deriveSeed(seed, kStageNoise));
    ValueNoise2D noise(noiseRng);
    Rng riverRng(Rng::deriveSeed(seed, kStageRiver));
    Rng cityRng(Rng::deriveSeed(seed, kStageCity));
    const double baseCell = std::max(w, h) / 6.0;

    // ① 海拔场。
    std::vector<double> height(static_cast<size_t>(w) * h, 0.0);
    std::vector<GradVec> gradVec(height.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            height[idx] = noise.fbm(x / baseCell, y / baseCell);
            double dx = 0.0, dy = 0.0;
            noise.gradient(x / baseCell, y / baseCell, 1.0 / 64.0, dx, dy);
            gradVec[idx] = {dx, dy};
        }
    }
    const TilingGeom squareGeom{TilingType::Square, w, h};
    return finishGeneratedTerrain(out, p, cityConfig, riverGen, squareGeom, height, gradVec, false,
                                  riverRng, cityRng);
}

// 六/三角密铺：海拔场每格中心**直接采样 fbm**（最朴素原始版，无任何平滑/平均/插值
// 后处理；2026-08-15 用户拍板回退，先以纯净基线定位"横纹/同向三角"现象），其余流程与
// 方形一致（分位数切海陆 + 内陆山 + 城权重），输出 native terrain records。
static bool generateTiled(MapDefinition& out, std::uint32_t seed, const MapGenParams& p,
                          const Config::City& cityConfig, const Config::River::Gen& riverGen) {
    const TilingGeom g{p.tiling, p.width, p.height};
    const int cellCount = g.cellCount();
    Rng noiseRng(Rng::deriveSeed(seed, kStageNoise));
    ValueNoise2D noise(noiseRng);
    Rng riverRng(Rng::deriveSeed(seed, kStageRiver));
    Rng cityRng(Rng::deriveSeed(seed, kStageCity));
    // 斜周期在周期基坐标中采样，避免世界坐标剪切噪声；gridCenter 保留块内基础格位置。
    const bool skew = g.hasSkewedPeriod();
    const double baseCell = skew
                                ? static_cast<double>(std::max(g.cols, g.rows)) / 6.0
                                : std::max(g.worldWidth(), g.worldHeight()) / 6.0;

    // ① 海拔场（skew：周期坐标中的格中心；否则：世界坐标中的格中心）。
    std::vector<double> height(static_cast<size_t>(cellCount), 0.0);
    std::vector<GradVec> gradVec(static_cast<size_t>(cellCount));
    for (int idx = 0; idx < cellCount; ++idx) {
        double gx, gy;
        if (skew)
            g.gridCenter(idx, gx, gy);
        else
            g.cellCenter(idx, gx, gy);
        height[static_cast<size_t>(idx)] = noise.fbm(gx / baseCell, gy / baseCell);
        double dx = 0.0, dy = 0.0;
        noise.gradient(gx / baseCell, gy / baseCell, 1.0 / 64.0, dx, dy);
        gradVec[static_cast<size_t>(idx)] = {dx, dy};
    }
    return finishGeneratedTerrain(out, p, cityConfig, riverGen, g, height, gradVec, skew, riverRng,
                                  cityRng);
}

}  // namespace lw
