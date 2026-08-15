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
#include <vector>

#include "core/Paths.h"
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

// 写 BMP 的最小头部（与 Map::loadFromBmp 的 54 字节头一致）。
void writeHeader(std::vector<unsigned char>& data, int w, int h) {
    data[0] = 'B';
    data[1] = 'M';
    const unsigned size = static_cast<unsigned>(data.size());
    std::memcpy(&data[2], &size, 4);
    const unsigned off = 54;
    std::memcpy(&data[10], &off, 4);
    const unsigned ih = 40;
    std::memcpy(&data[14], &ih, 4);
    const int iw = w;
    std::memcpy(&data[18], &iw, 4);
    const int ihh = h;
    std::memcpy(&data[22], &ihh, 4);
    const unsigned short planes = 1;
    std::memcpy(&data[26], &planes, 2);
    const unsigned short bpp = 24;
    std::memcpy(&data[28], &bpp, 2);
}

}  // namespace

// 前向声明（generate 分派用；实现见文件后部）。
static bool generateSquare(const std::string& path, std::uint32_t seed, const MapGenParams& p);
static bool generateTiled(const std::string& path, std::uint32_t seed, const MapGenParams& p);

std::string MapGenerator::defaultPath(std::uint32_t seed, const MapGenParams& p) {
    char buf[192];
    // 2026-08 工程改进：生成物入 userdata/maps/（运行期产物，不进版本库）。
    if (p.tiling == TilingType::Square) {
        std::snprintf(buf, sizeof(buf), "%s/gen_%u_%dx%d_%.2f_%.2f_%.2f.bmp", kGeneratedMapDir, seed,
                      p.width, p.height, p.seaRatio, p.mountainDensity, p.cityDensity);
    } else {
        std::snprintf(buf, sizeof(buf), "%s/gen_%u_%s_%dx%d_%.2f_%.2f_%.2f.lwmap",
                      kGeneratedMapDir, seed, tilingName(p.tiling), p.width, p.height, p.seaRatio,
                      p.mountainDensity, p.cityDensity);
    }
    return buf;
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
    MapGenParams p = raw;
    p.width = std::clamp(p.width, 32, 200);
    p.height = std::clamp(p.height, 32, 200);
    p.seaRatio = std::clamp(p.seaRatio, 0.0, 0.90);  // 0 = 全陆地；0.9 = 陆地占比下界 0.1（2026-08-06）
    p.mountainDensity = std::clamp(p.mountainDensity, 0.0, 0.9);
    p.cityDensity = std::clamp(p.cityDensity, 0.0, 0.9);
    // P12：六/三角行数 clamp 到偶数（垂直环绕闭合必需，见开发计划 P12 §2.3）。
    // 半正/Laves 表驱动用矩形周期域，任意行数均可。
    if ((p.tiling == TilingType::Hex || p.tiling == TilingType::Tri) && (p.height & 1))
        --p.height;
    // P12（2026-08 用户拍板）：三角**列数减半**——width 语义 = 视觉列数（每行三角数），
    // 生成时列对数 = width/2 → cellCount = width×height 与方形一致；并强制列数为偶数
    // （(x/2)&~1：105→52、106→52、108→54）。与 Map::configure 同一公式（lwmap 尺寸匹配）。
    if (p.tiling == TilingType::Tri) p.width = (p.width / 2) & ~1;

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

    // ④ 城（地形作用：沿海/低地密集、山顶几乎不建城）：
    //    权重 = 沿海×2 × 山顶×0.15 × 低地偏好(1.3 - 0.6·hNorm)，归一化到期望 Σp = cityDensity×陆格数。
    size_t landCount = 0;
    double maxLandH = threshold;
    for (size_t idx = 0; idx < land.size(); ++idx) {
        if (!land[idx]) continue;
        ++landCount;
        maxLandH = std::max(maxLandH, height[idx]);
    }
    const double hSpan = std::max(1e-9, maxLandH - threshold);
    std::vector<double> cityWeight(land.size(), 0.0);
    double cityWeightSum = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            if (!land[idx]) continue;
            double wgt = 1.0;
            if (adjacentSea(x, y)) wgt *= 2.0;  // 沿海城市偏好
            if (isMountain[idx]) {
                wgt *= 0.15;                    // 山顶不建城
            } else {
                const double hNorm =
                    std::clamp((height[idx] - threshold) / hSpan, 0.0, 1.0);
                wgt *= (1.3 - 0.6 * hNorm);     // 低地偏好（高海拔陆格权重更低）
            }
            cityWeight[idx] = wgt;
            cityWeightSum += wgt;
        }
    }

    // ⑤ 编码像素 → 写 BMP（文件行 j = 世界行 j，与 Map::loadFromBmp 逐行对齐）。
    const int rowsize = w * 3 + 1;
    std::vector<unsigned char> data(54 + static_cast<size_t>(h) * rowsize, 0);
    writeHeader(data, w, h);
    const auto enc = [](double p) -> unsigned char {
        int v = 128 + static_cast<int>(std::lround(127.0 * p));
        return static_cast<unsigned char>(std::clamp(v, 128, 255));
    };
    for (int y = 0; y < h; ++y) {
        size_t rowStart = 54 + static_cast<size_t>(y) * rowsize;
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
                G = enc(std::min(pCity, 1.0));
                B = 32;  // 闲置通道；须 ≥ seaChannelMin(32) 才不判海
            }
            data[rowStart + static_cast<size_t>(x) * 3 + 0] = B;
            data[rowStart + static_cast<size_t>(x) * 3 + 1] = G;
            data[rowStart + static_cast<size_t>(x) * 3 + 2] = R;
        }
        // 行尾 1 字节填充保持 0（与读取器 width*3+1 对齐）。
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        spdlog::error("MapGenerator: cannot open '{}' for write", path);
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!ofs) {
        spdlog::error("MapGenerator: write to '{}' failed", path);
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
    const double baseCell = std::max(g.worldWidth(), g.worldHeight()) / 6.0;

    // ① 海拔场（最朴素：每格中心独立采样同一张连续 fbm 噪声场，格间无任何耦合/后处理）。
    std::vector<double> height(static_cast<size_t>(cellCount), 0.0);
    for (int idx = 0; idx < cellCount; ++idx) {
        double wx, wy;
        g.cellCenter(idx, wx, wy);
        height[static_cast<size_t>(idx)] = noise.fbm(wx / baseCell, wy / baseCell);
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
    const double ebandW = std::max(4.0, std::min(g.worldWidth(), g.worldHeight()) / 12.0);
    constexpr double kEdgeStrength = 0.5;
    if (p.forceCoast) {
        for (int idx = 0; idx < cellCount; ++idx) {
            double wx, wy;
            g.cellCenter(idx, wx, wy);
            const double d = std::min({wx, g.worldWidth() - wx, wy, g.worldHeight() - wy});
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
    size_t eligibleCount = 0;
    for (int idx = 0; idx < cellCount; ++idx)
        if (land[static_cast<size_t>(idx)] && !adjacentSea(idx)) {
            orderH.push_back(static_cast<size_t>(idx));
            orderG.push_back(static_cast<size_t>(idx));
            ++eligibleCount;
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

    // ④ 城权重（沿海×2 × 山顶×0.15 × 低地偏好），归一化到期望 Σp = cityDensity×陆格数。
    size_t landCount = 0;
    double maxLandH = threshold;
    for (size_t idx = 0; idx < land.size(); ++idx) {
        if (!land[idx]) continue;
        ++landCount;
        maxLandH = std::max(maxLandH, height[idx]);
    }
    const double hSpan = std::max(1e-9, maxLandH - threshold);
    std::vector<double> cityWeight(land.size(), 0.0);
    double cityWeightSum = 0.0;
    for (int idx = 0; idx < cellCount; ++idx) {
        if (!land[static_cast<size_t>(idx)]) continue;
        double wgt = 1.0;
        if (adjacentSea(idx)) wgt *= 2.0;
        if (isMountain[static_cast<size_t>(idx)]) {
            wgt *= 0.15;
        } else {
            const double hNorm = std::clamp(
                (height[static_cast<size_t>(idx)] - threshold) / hSpan, 0.0, 1.0);
            wgt *= (1.3 - 0.6 * hNorm);
        }
        cityWeight[static_cast<size_t>(idx)] = wgt;
        cityWeightSum += wgt;
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
    const auto enc = [](double pp) -> unsigned char {
        int v = 128 + static_cast<int>(std::lround(127.0 * pp));
        return static_cast<unsigned char>(std::clamp(v, 128, 255));
    };
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
            G = enc(std::min(pCity, 1.0));
            B = 32;  // 闲置通道；须 ≥ seaChannelMin(32) 才不判海
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
