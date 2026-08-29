// MapGenerator.cpp — 随机地图生成实现（开发计划 P6）。
// 算法（确定性）：① Rng(seed) 洗牌生成 256 值噪声查表 → ② fBm 海拔场 h∈[0,1] →
// ③ 排序取 seaRatio 分位数切海陆（可选强制边缘为海）→ ④ 山 = 高原分量（按基础海拔取前段，
// 大块高地）+ 山脉分量（按独立种子山脊噪声取前段，蜿蜒山脊）→ ⑤ 城概率按地形权重 →
// ⑥ 编码 R/G 概率通道写 BMP。
#include "world/MapGenerator.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "core/Paths.h"
#include "world/Bmp24.h"
#include "world/TerrainCodec.h"
#include "core/Random.h"
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

}  // namespace

// 前向声明（generate 分派用；实现见文件后部）。
static bool generateSquare(const std::string& path, std::uint32_t seed, const MapGenParams& p);
static bool generateTiled(const std::string& path, std::uint32_t seed, const MapGenParams& p);

MapGenParams normalizedParams(const MapGenParams& raw) {
    MapGenParams p = raw;
    p.width = std::clamp(p.width, 32, 200);
    p.height = std::clamp(p.height, 32, 200);
    p.seaRatio = std::clamp(p.seaRatio, 0.0, 0.90);
    p.mountainDensity = std::clamp(p.mountainDensity, 0.0, 0.9);
    p.cityDensity = std::clamp(p.cityDensity, 0.0, 0.9);
    int cols = p.width, rows = p.height;
    chooseTableDomain(static_cast<int>(p.tiling), p.width, p.height, cols, rows);
    p.width = cols;
    p.height = rows;
    return p;
}

std::string MapGenerator::defaultPath(std::uint32_t seed, const MapGenParams& p) {
    const MapGenParams n = normalizedParams(p);
    std::ostringstream key;
    key << kGeneratedMapDir << "/gen_" << seed << "_" << tilingName(n.tiling) << "_"
        << n.width << "x" << n.height << "_sea" << std::fixed << std::setprecision(9)
        << n.seaRatio << "_mtn" << n.mountainDensity << "_city" << n.cityDensity
        << "_coast" << (n.forceCoast ? 1 : 0)
        << (n.tiling == TilingType::Square ? ".bmp" : ".lwmap");
    return key.str();
}

bool MapGenerator::generate(const std::string& path, std::uint32_t seed, const MapGenParams& raw) {
    // 写盘前确保父目录存在（首次运行 userdata/maps/ 尚不存在）。
    const std::size_t slash = path.find_last_of("/\\");
    const std::string dir =
        (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
    if (!ensureDirExists(dir)) {
        spdlog::error("map generate: cannot create dir '{}'", dir);
        return false;
    }
    MapGenParams p = normalizedParams(raw);

    if (p.tiling == TilingType::Square) return generateSquare(path, seed, p);
    return generateTiled(path, seed, p);
}

// 正方形密铺（现状路径，零改动）：生成 BMP。文件局部，不导出。
static bool generateSquare(const std::string& path, std::uint32_t seed, const MapGenParams& p) {
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
    // ①a 坡度场（供"山脉"分量选山用）：海拔梯度大的格 = 高原边缘/内部山脊的陡坡，
    //     沿等高线构成蜿蜒山脉（环绕高原主体），与"高原"分量互补。
    std::vector<double> grad(static_cast<size_t>(w) * h, 0.0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double hv = height[static_cast<size_t>(y) * w + x];
            double g = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    g = std::max(g, std::abs(hv - height[static_cast<size_t>(ny) * w + nx]));
                }
            }
            grad[static_cast<size_t>(y) * w + x] = g;
        }
    }

    // ①b 强制边缘为海（2026-08-06 用户要求）：边缘带按到边缘的距离 d 削减海拔，
    //     越边缘削越多（线性衰减，带宽 band、边缘强度 0.5）→ 海自然从边缘向里延伸；
    //     最外一圈（d==0）恒为海（见 ②b），故陆地占比 100% 时也只有恰好一圈海。
    const int edgeBand = std::max(4, std::min(w, h) / 12);
    constexpr double kEdgeStrength = 0.5;
    if (p.forceCoast) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int d = std::min({x, w - 1 - x, y, h - 1 - y});
                if (d < edgeBand) {
                    const double atten = kEdgeStrength * (1.0 - static_cast<double>(d) / edgeBand);
                    height[static_cast<size_t>(y) * w + x] =
                        std::max(0.0, height[static_cast<size_t>(y) * w + x] - atten);
                }
            }
        }
    }

    // ② 海/陆阈值：排序后第 seaRatio 分位数；h < threshold → 海。
    std::vector<double> sorted = height;
    std::sort(sorted.begin(), sorted.end());
    const size_t k = std::min(static_cast<size_t>(static_cast<double>(sorted.size()) * p.seaRatio),
                              sorted.size() - 1);
    const double threshold = sorted[k];

    std::vector<bool> land(static_cast<size_t>(w) * h, false);
    for (size_t i = 0; i < land.size(); ++i) land[i] = height[i] >= threshold;
    // ②b 强制边缘为海：最外一圈（d==0）必为海（即便 seaRatio=0 全陆地，也只有这圈海）。
    if (p.forceCoast) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (std::min({x, w - 1 - x, y, h - 1 - y}) == 0)
                    land[static_cast<size_t>(y) * w + x] = false;
            }
        }
    }

    // ③ 山（地形作用：成片山脉，2026-08-06 用户反馈"分布太均匀"）：
    //    内陆格（陆且 8 邻无海）→ R=255（确定性山，chance(1.0) 恒真）。
    const auto adjacentSea = [&](int x, int y) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                if (!land[static_cast<size_t>(ny) * w + nx]) return true;
            }
        return false;
    };
    std::vector<bool> eligible(land.size(), false);
    size_t eligibleCount = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            if (land[idx] && !adjacentSea(x, y)) {
                eligible[idx] = true;
                ++eligibleCount;
            }
        }
    }
    // ③b 高原 + 山脉双分量（2026-08-06 用户反馈"全是高原"）：
    //     总山数 mtnCount = mountainDensity × 内陆格数，按 高原/山脉 各半分配：
    //       - 高原分量：取内陆格按【基础海拔】最高的前 nPlateau → 大块高地（高原形状）；
    //       - 山脉分量：取内陆格按【坡度】最高的前 nRange → 沿高原边缘/山脊的蜿蜒山脉。
    std::vector<size_t> orderH, orderG;
    orderH.reserve(eligibleCount);
    orderG.reserve(eligibleCount);
    for (size_t idx = 0; idx < land.size(); ++idx) {
        if (!eligible[idx]) continue;
        orderH.push_back(idx);
        orderG.push_back(idx);
    }
    std::sort(orderH.begin(), orderH.end(), [&](size_t a, size_t b) {
        if (height[a] != height[b]) return height[a] > height[b];
        return a < b;
    });
    std::sort(orderG.begin(), orderG.end(), [&](size_t a, size_t b) {
        if (grad[a] != grad[b]) return grad[a] > grad[b];  // 坡越陡越像山脉
        return a < b;
    });
    std::vector<bool> isMountain(land.size(), false);
    constexpr double kPlateauShare = 0.4;  // 山数中高原占比（其余 0.6 为边缘/山脊山脉）
    const size_t mtnCount = static_cast<size_t>(std::llround(p.mountainDensity * orderH.size()));
    const size_t nPlateau = static_cast<size_t>(std::llround(mtnCount * kPlateauShare));
    const size_t nRange = mtnCount - nPlateau;
    for (size_t k = 0; k < nPlateau && k < orderH.size(); ++k) isMountain[orderH[k]] = true;
    for (size_t k = 0; k < nRange && k < orderG.size(); ++k) isMountain[orderG[k]] = true;

    // ④ 城：取消沿海/海拔偏好；保留山地惩罚 ×0.3。
    size_t landCount = 0;
    std::vector<double> cityWeight(land.size(), 0.0);
    double cityWeightSum = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            if (!land[idx]) continue;
            const double wgt = isMountain[idx] ? p.cityMountainWeight : 1.0;  // 山地降低城市率
            cityWeight[idx] = wgt;
            cityWeightSum += wgt;
            ++landCount;
        }
    }

    // ⑤ 编码像素 → 写 BMP（文件行 j = 世界行 j，与 Map::loadFromBmp 逐行对齐）。
    std::vector<std::array<unsigned char, 3>> pixels(static_cast<size_t>(w) * h);
    const Config::Terrain terrainConfig{};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            unsigned char R, G, B;
            if (!land[idx]) {
                R = G = B = 0;  // 任一分量 <32 → 海（确定性）
            } else {
                R = isMountain[idx] ? 255u : 32u;  // 山：确定性（chance(1.0) 恒真）
                double pCity = 0.0;
                if (cityWeightSum > 0.0) {
                    pCity = p.cityDensity * static_cast<double>(landCount) *
                            cityWeight[idx] / cityWeightSum;
                }
                G = terrain::encodeProbability(pCity, terrainConfig);
                B = static_cast<unsigned char>(terrainConfig.seaChannelMin);
            }
            pixels[static_cast<size_t>(y) * w + x] = {R, G, B};
        }
    }

    std::string error;
    if (!writeBmp24(path, w, h, pixels, &error)) {
        spdlog::error("MapGenerator: '{}' write failed: {}", path, error);
        return false;
    }
    spdlog::info("MapGenerator: '{}' written ({}x{}, sea {:.2f}, mtn {:.2f}, city {:.2f})", path, w,
                 h, p.seaRatio, p.mountainDensity, p.cityDensity);
    return true;
}

// 六/三角密铺：海拔场每格中心**直接采样 fbm**（最朴素原始版，无任何平滑/平均/插值
// 后处理；2026-08-15 用户拍板回退，先以纯净基线定位"横纹/同向三角"现象），其余流程与
// 方形一致（分位数切海陆 + 内陆山 + 城权重），输出 lwmap（BGR 通道，与 Map::loadFromLwmap
// 逐格对齐）。文件局部。
static bool generateTiled(const std::string& path, std::uint32_t seed, const MapGenParams& p) {
    const TilingGeom g{p.tiling, p.width, p.height};
    const int cellCount = g.cellCount();
    Rng rng(seed);
    ValueNoise2D noise(rng);
    // 2026-08 斜周期（33336 系）：世界为平行四边形（W.y/H.x 剪切），若按世界坐标采样，
    // 噪声场被剪切 → 地形呈斜纹/陆地占比失真。改为按**格坐标**（c,r 常规矩形）采样——
    // 即"旋转到常规矩形后再采样"，地形在各向同性矩形域上生成，格后再映射回斜周期。
    const bool skew = g.hasSkewedPeriod();
    const double baseCell = skew
                                ? static_cast<double>(std::max(g.cols, g.rows)) / 6.0
                                : std::max(g.worldWidth(), g.worldHeight()) / 6.0;
    // 采样坐标（格坐标 [c,r] 或世界坐标 [wx,wy]）。
    std::vector<int> sampC(static_cast<size_t>(cellCount)), sampR(static_cast<size_t>(cellCount));
    if (skew) {
        for (int idx = 0; idx < cellCount; ++idx) {
            int r, c, b;
            g.indexToRowCol(idx, r, c, b);
            sampC[static_cast<size_t>(idx)] = c;
            sampR[static_cast<size_t>(idx)] = r;
        }
    }

    // ① 海拔场（skew：按格坐标在各向同性矩形域采样；否则按格中心世界坐标采样）。
    std::vector<double> height(static_cast<size_t>(cellCount), 0.0);
    for (int idx = 0; idx < cellCount; ++idx) {
        double gx, gy;
        if (skew) {
            gx = static_cast<double>(sampC[static_cast<size_t>(idx)]);
            gy = static_cast<double>(sampR[static_cast<size_t>(idx)]);
        } else {
            g.cellCenter(idx, gx, gy);
        }
        height[static_cast<size_t>(idx)] = noise.fbm(gx / baseCell, gy / baseCell);
    }
    // ①a 坡度场（山脉分量）：边邻海拔差最大。
    std::vector<double> grad(static_cast<size_t>(cellCount), 0.0);
    for (int idx = 0; idx < cellCount; ++idx) {
        const double hv = height[static_cast<size_t>(idx)];
        double gr = 0.0;
        for (int k = 0; k < g.neighborCount(); ++k) {
            const int nb = g.neighbor(idx, k);
            if (nb < 0) continue;
            gr = std::max(gr, std::fabs(hv - height[static_cast<size_t>(nb)]));
        }
        grad[static_cast<size_t>(idx)] = gr;
    }

    // ①b 强制边缘为海（决策 11：任意密铺生效）：按格**真实世界坐标**到地图矩形边缘的
    //   欧氏距离削减海拔；外圈恒海。2026-08 修正：旧实现按格索引 (r,c) 估距离，在六/三角
    //   上失真——三角正/反格共享同一 (r,c) 却被减同一衰减（趋海被拉平、成片同向三角），
    //   且削减带沿整行/整列垂直延伸，经阈值量化后横贯整行成直线海陆边（"横纹"）。
    //   改用 cellCenter 世界坐标 → d = min(wx, worldW-wx, wy, worldH-wy)，贴合直边布局；
    //   edgeBand 由"格数"改以 baseCell（世界尺度）为单位。
    //   2026-08 斜周期：worldWidth/Height 为平行四边形 AABB（被剪切撑大），世界坐标距边
    //   的欧氏距离不再贴合"视觉矩形边缘"——改用格坐标（c,r 到 [0,cols]x[0,rows] 边缘距离）
    //   在**常规矩形域**内计算衰减，与采样一致。
    const double ebandW = skew
                              ? std::max(4.0, static_cast<double>(std::min(g.cols, g.rows)) / 6.0)
                              : std::max(4.0, std::min(g.worldWidth(), g.worldHeight()) / 12.0);
    constexpr double kEdgeStrength = 0.5;
    if (p.forceCoast) {
        for (int idx = 0; idx < cellCount; ++idx) {
            double d;
            if (skew) {
                const double c0 = sampC[static_cast<size_t>(idx)], r0 = sampR[static_cast<size_t>(idx)];
                d = std::min({c0, static_cast<double>(g.cols - 1) - c0, r0,
                              static_cast<double>(g.rows - 1) - r0});
            } else {
                double wx, wy;
                g.cellCenter(idx, wx, wy);
                d = std::min({wx, g.worldWidth() - wx, wy, g.worldHeight() - wy});
            }
            if (d < ebandW) {
                const double atten = kEdgeStrength * (1.0 - d / ebandW);
                height[static_cast<size_t>(idx)] =
                    std::max(0.0, height[static_cast<size_t>(idx)] - atten);
            }
        }
    }

    // ② 海/陆阈值（排序分位数）+ 外圈恒海。
    std::vector<double> sorted = height;
    std::sort(sorted.begin(), sorted.end());
    const size_t k = std::min(static_cast<size_t>(static_cast<double>(sorted.size()) * p.seaRatio),
                              sorted.size() - 1);
    const double threshold = sorted[k];
    std::vector<bool> land(static_cast<size_t>(cellCount), false);
    for (size_t i = 0; i < land.size(); ++i) land[i] = height[i] >= threshold;
    // ②b 外圈恒海：按密铺**行/列索引**到边界距离 == 0（与方形 d==0 语义一致——用户
    //   规范"勾选强制为海且 100% 陆地时，只有最外围一圈是海"）。
    //   hex：r==0||r==rows-1||c==0||c==cols-1——含**凹进的偶数行最右列**（其右顶点距
    //   右缘 √3a/2 不贴边，但视觉上属最右列带）；世界坐标距离判定会把 hex 左第二列
    //   （距左缘同样 √3a/2）与右最列混为一谈，无法两全，索引语义才与方形一致。
    //   tri：r==0||r==rows-1||i==0||i==cols-1（每行 cols 个三角对；最左/最右对 =
    //   视觉最外列带，含右缘尖出对与左缘 j=0 贴边对）。
    if (p.forceCoast) {
        const int B = g.baseCount();
        for (int r = 0; r < g.rows; ++r) {
            for (int c = 0; c < g.cols; ++c) {
                const bool outer = (r == 0 || r == g.rows - 1 || c == 0 || c == g.cols - 1);
                if (!outer) continue;
                for (int b = 0; b < B; ++b) {
                    const int idx = g.cellIndexAt(r, c, b);
                    if (idx >= 0) land[static_cast<size_t>(idx)] = false;
                }
            }
        }
    }

    // ③ 山（内陆：边邻无海 → 确定性山 R=255）。
    const auto adjacentSea = [&](int idx) {
        for (int k = 0; k < g.neighborCount(idx); ++k) {
            const int nb = g.neighbor(idx, k);
            if (nb >= 0 && !land[static_cast<size_t>(nb)]) return true;
        }
        return false;
    };
    std::vector<size_t> orderH, orderG;
    for (int idx = 0; idx < cellCount; ++idx)
        if (land[static_cast<size_t>(idx)] && !adjacentSea(idx)) {
            orderH.push_back(static_cast<size_t>(idx));
            orderG.push_back(static_cast<size_t>(idx));
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
    const size_t mtnCount = static_cast<size_t>(std::llround(p.mountainDensity * orderH.size()));
    const size_t nPlateau = static_cast<size_t>(std::llround(mtnCount * kPlateauShare));
    const size_t nRange = mtnCount - nPlateau;
    for (size_t kk = 0; kk < nPlateau && kk < orderH.size(); ++kk) isMountain[orderH[kk]] = true;
    for (size_t kk = 0; kk < nRange && kk < orderG.size(); ++kk) isMountain[orderG[kk]] = true;

    // ④ 城：取消沿海/海拔偏好；保留山地惩罚 ×0.3。
    size_t landCount = 0;
    std::vector<double> cityWeight(land.size(), 0.0);
    double cityWeightSum = 0.0;
    for (int idx = 0; idx < cellCount; ++idx) {
        if (!land[static_cast<size_t>(idx)]) continue;
        const double wgt = isMountain[static_cast<size_t>(idx)] ? p.cityMountainWeight : 1.0;
        cityWeight[static_cast<size_t>(idx)] = wgt;
        cityWeightSum += wgt;
        ++landCount;
    }

    // ⑤ 编码 → 写 lwmap（magic "LWMP" + ver1 + tiling + cols + rows + 逐格 BGR 通道）。
    std::vector<unsigned char> data;
    data.reserve(static_cast<size_t>(10 + cellCount * 3));
    data.insert(data.end(), {'L', 'W', 'M', 'P'});
    data.push_back(1);  // version
    data.push_back(static_cast<unsigned char>(static_cast<int>(p.tiling)));
    const auto push32 = [&](int v) {
        data.push_back(static_cast<unsigned char>(v & 0xFF));
        data.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        data.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
        data.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
    };
    push32(g.cols);
    push32(g.rows);
    const Config::Terrain terrainConfig{};
    for (int idx = 0; idx < cellCount; ++idx) {
        unsigned char R, G, B;
        if (!land[static_cast<size_t>(idx)]) {
            R = G = B = 0;  // 任一分量 <32 → 海（确定性）
        } else {
            R = isMountain[static_cast<size_t>(idx)] ? 255u : 32u;
            double pCity = 0.0;
            if (cityWeightSum > 0.0)
                pCity = p.cityDensity * static_cast<double>(landCount)
                        * cityWeight[static_cast<size_t>(idx)] / cityWeightSum;
            G = terrain::encodeProbability(pCity, terrainConfig);
            B = static_cast<unsigned char>(terrainConfig.seaChannelMin);
        }
        data.push_back(B);
        data.push_back(G);
        data.push_back(R);
    }
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        spdlog::error("MapGenerator: cannot open '{}' for write", path);
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!ofs) {
        spdlog::error("MapGenerator: write to '{}' failed", path);
        return false;
    }
    spdlog::info("MapGenerator: '{}' written ({} {}x{}, sea {:.2f}, mtn {:.2f}, city {:.2f})", path,
                 tilingName(p.tiling), g.cols, g.rows, p.seaRatio, p.mountainDensity,
                 p.cityDensity);
    return true;
}

}  // namespace lw
