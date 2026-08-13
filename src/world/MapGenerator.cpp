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

std::string MapGenerator::defaultPath(std::uint32_t seed, const MapGenParams& p) {
    char buf[160];
    // 2026-08 工程改进：生成物入 userdata/maps/（运行期产物，不进版本库）。
    std::snprintf(buf, sizeof(buf), "%s/gen_%u_%dx%d_%.2f_%.2f_%.2f.bmp", kGeneratedMapDir, seed,
                  p.width, p.height, p.seaRatio, p.mountainDensity, p.cityDensity);
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

}  // namespace lw
