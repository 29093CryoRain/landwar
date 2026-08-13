#include "world/Map.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <fstream>

#include "core/MathUtil.h"

namespace lw {

namespace {
// 首都重试上限：仅防病态地图死循环（正常地图远用不到，不影响确定性路径）。
constexpr int kCapitalMaxAttempts = 1000000;

// 概率 ramp：p = clamp((channel - probFloor) / probScale, 0, 1)。
// channel <= floor → 0；channel == floor+scale → 1（线性）。山用 R、城用 G 通道。
double rampProb(int channel, const Config::Terrain& t) {
    if (channel <= t.probFloor) return 0.0;
    const double p = static_cast<double>(channel - t.probFloor) / t.probScale;
    return std::min(p, 1.0);
}

}  // namespace

int Map::sampleCityLevel(const Config::City& cc, Rng& rng) {
    // P13：等级采样（幂律分布，思路 9.1）。s = alpha + beta（config），
    //   P(L=levels[i]) = levels[i]^{-s} - levels[i+1]^{-s}（末级 = levels[N-1]^{-s}），
    //   即 P(L=1)=1-2^{-s}; P(L=2)=2^{-s}-4^{-s}; P(L=4)=4^{-s}-6^{-s};
    //       P(L=6)=6^{-s}-9^{-s}; P(L=9)=9^{-s}
    // u = rng.unit() 按累计概率确定等级（1 次 RNG）。levels 须升序（内置 1/2/4/6/9）。
    const double s = cc.rankExponent();
    const double u = rng.unit();
    double cum = 0.0;
    for (int i = 0; i < static_cast<int>(cc.levels.size()); ++i) {
        const double cur = std::pow(static_cast<double>(cc.levels[static_cast<size_t>(i)]), -s);
        const double next = (i + 1 < static_cast<int>(cc.levels.size()))
                                ? std::pow(static_cast<double>(cc.levels[static_cast<size_t>(i + 1)]),
                                           -s)
                                : 0.0;
        cum += cur - next;  // P(L=levels[i]) = levels[i]^-s - levels[i+1]^-s
        if (u < cum) return cc.levels[static_cast<size_t>(i)];
    }
    return cc.levels.back();  // 浮点兜底（累计 ≈ 1）
}

void Map::clear() {
    cells_.assign(static_cast<size_t>(width_) * height_, MapCell{});
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            MapCell& c = at(x, y);
            c = MapCell{};
            c.x = x;
            c.y = y;
        }
    }
    capitalX_.clear();
    capitalY_.clear();
    cities_.clear();
}

void Map::configure(const Config::Map& cfg) {
    width_ = cfg.width;
    height_ = cfg.height;
    blockSize_ = cfg.blockSize;
    panelWidth_ = cfg.panelWidth;
    capitalMinDistance_ = cfg.capitalMinDistance;
    clear();
}

bool Map::loadFromBmp(const std::string& path, Rng& rng) {
    std::ifstream ifs(path, std::ios::binary);
    // MinGW：文件不存在须用 is_open() 判断（Phase 1 已踩坑）。
    if (!ifs.is_open()) {
        spdlog::error("map file '{}' not found", path);
        return false;
    }

    // 54 字节 BMP 头（BITMAPFILEHEADER + BITMAPINFOHEADER，已核实 offBits=54）。
    char header[54];
    ifs.read(header, 54);
    if (ifs.gcount() != 54) {
        spdlog::error("map file '{}': header truncated", path);
        return false;
    }

    // P13 第一遍：读全图通道 → 确定海/陆（确定性，不掷骰）。城市形状占用后续格，须先全图
    // 海陆已知才能做放置检查（第二遍），故拆两遍。
    struct CellChannels {
        bool sea;
        int g;
        int r;
    };
    std::vector<CellChannels> ch(static_cast<size_t>(width_) * height_);
    for (int j = 0; j < height_; ++j) {
        for (int i = 0; i < width_; ++i) {
            unsigned char bgr[3];
            ifs.read(reinterpret_cast<char*>(bgr), 3);
            if (ifs.gcount() != 3) {
                spdlog::error("map file '{}': pixel data truncated at ({},{})", path, i, j);
                return false;
            }
            const bool sea = std::min({bgr[2], bgr[1], bgr[0]}) < terrain_.seaChannelMin;
            // P13 关键：第一遍即确定全图海陆（置 cell.land）。否则第二遍逐格时才设 land，
            // 多格形状要检查的"未来格"（同行右侧/下方行）land 仍是 clear 默认 false，
            // 放置恒失败回退 1 级（2026-08-07 修：测试暴露多格城市全部塌缩为 1 级）。
            at(i, j).land = !sea;
            ch[static_cast<size_t>(j) * width_ + i] = {sea, bgr[1], bgr[2]};
        }
        char pad;
        ifs.read(&pad, 1);
        if (ifs.gcount() != 1) {
            spdlog::error("map file '{}': row padding truncated at row {}", path, j);
            return false;
        }
    }

    // 第二遍：原迭代序（y 先行后列）。海/陆确定性；每陆格掷山骰一次（恒），城概率仅在未被
    // 既有城市形状占用的格上掷（占用格跳过 → 不掷城概率，RNG 位置确定性）；城概率通过 →
    // 先采样等级、再尝试放置（锚点 = 当前格），失败回退 1 级。
    for (int j = 0; j < height_; ++j) {
        for (int i = 0; i < width_; ++i) {
            const CellChannels& cc = ch[static_cast<size_t>(j) * width_ + i];
            MapCell& cell = at(i, j);
            cell.x = i;
            cell.y = j;
            cell.belongi = 0;
            cell.cityAllowed = false;
            cell.mountain = false;
            // cityId 保留原值（clear 置 -1；已被前面放置的形状占用则不变）。
            if (cc.sea) {
                cell.cityId = -1;
                continue;  // land 已在第一遍置 false（海）；本遍只收尾
            }
            cell.cityAllowed = rampProb(cc.g, terrain_) > 0.0;
            if (cell.cityId != -1) {
                // 已被既有城市形状占用：不掷城概率（RNG 位置确定性），仍掷山骰。
                cell.mountain = rng.chance(rampProb(cc.r, terrain_));
                continue;
            }
            if (rng.chance(rampProb(cc.g, terrain_))) {
                const int level = sampleCityLevel(cityConfig_, rng);
                if (canPlaceCity(level, i, j)) {
                    addCity(level, i, j);
                } else if (canPlaceCity(1, i, j)) {
                    addCity(1, i, j);  // 放置失败回退 1 级
                }
            }
            cell.mountain = rng.chance(rampProb(cc.r, terrain_));
        }
    }
    correctMountainCoast();  // 邻海修正：山只出现在不与海相邻的陆地上
    return true;
}

void Map::correctMountainCoast() {
    // 8 邻域（含对角）含海（land==false）→ 山置普通陆。只读 land（本循环不改 land），
    // 结果与迭代顺序无关（确定性）；地图边界外不算海（不越界）。
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            MapCell& c = at(x, y);
            if (!c.mountain) continue;
            bool nearSea = false;
            for (int dy = -1; dy <= 1 && !nearSea; ++dy) {
                for (int dx = -1; dx <= 1 && !nearSea; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                    if (!at(nx, ny).land) nearSea = true;
                }
            }
            if (nearSea) c.mountain = false;
        }
    }
}

bool Map::placeCapitals(Rng& rng) {
    capitalX_.assign(static_cast<size_t>(kPlayerFactionCount), -9961);
    capitalY_.assign(static_cast<size_t>(kPlayerFactionCount), -9961);
    int attempts = 0;
    // P5 改版：首都优先落在"可产城"格（cityAllowed = 基图允许该格成为城市，即源 g>probFloor）。
    // 这样基图"必然无城"的格子（ramp(G)=0）不会被首都强行塞进城市。
    // 每放一个首都前，先判断是否还有"可用可产城格"（未被占用且距已放首都足够远）：
    //   有 → 该首都只接受可产城格；无 → 放宽到任意陆地（保证 8 首都总能放下）。
    // P6 稀疏地图兜底（2026-08-06）：8 首都按全距离放不下（如 90% 海随机图）→ 距离降档
    //   （减半）重试；正常地图远用不到 → RNG 序列不变，黄金数据/基线不受影响。
    int minDist = capitalMinDistance_;
    // 扫描"还有可用可产城格"（用给定 dist；minDist 降档时重扫）。
    const auto scanUsable = [&](int dist, int placedCount) -> bool {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const MapCell& c = at(x, y);
                if (!c.land || !c.cityAllowed) continue;
                bool far = true;
                for (int j = 0; j < placedCount; ++j) {
                    if (math::distance(x, y, capitalX_[static_cast<size_t>(j)],
                                       capitalY_[static_cast<size_t>(j)]) < dist) {
                        far = false;
                        break;
                    }
                }
                if (far) return true;
            }
        }
        return false;
    };
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        capitalX_[static_cast<size_t>(i)] = -9961;
        capitalY_[static_cast<size_t>(i)] = -9961;
        bool anyUsableCityCell = scanUsable(minDist, i);
        // 失败重试语义与原版一致（while 内 retry 同一 slot）。
        while (true) {
            const int rx = rng.get(width_ - 1);
            const int ry = rng.get(height_ - 1);
            ++attempts;
            if (attempts > kCapitalMaxAttempts) {
                // 距离降档：按当前距离放不下 → 减半重试（稀疏地图；正常地图不触发）。
                if (minDist <= 1) {
                    spdlog::error("placeCapitals: retry exhausted (map too sparse?)");
                    return false;
                }
                minDist = std::max(1, minDist / 2);
                attempts = 0;
                anyUsableCityCell = scanUsable(minDist, i);
                continue;
            }
            if (!at(rx, ry).land) continue;
            if (anyUsableCityCell && !at(rx, ry).cityAllowed) continue;  // 还有可产城格可用 → 只收它
            bool tooClose = false;
            for (int j = 0; j < i; ++j) {
                if (math::distance(rx, ry, capitalX_[static_cast<size_t>(j)],
                                   capitalY_[static_cast<size_t>(j)]) < minDist) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;
            capitalX_[static_cast<size_t>(i)] = rx;
            capitalY_[static_cast<size_t>(i)] = ry;
            break;
        }
    }
    // P13：锚点格已是城市（loadFromBmp 放置的形状）→ 该城即首都（不新建）；否则注册 1 级城。
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        const int cx = capitalX_[static_cast<size_t>(i)];
        const int cy = capitalY_[static_cast<size_t>(i)];
        if (cx < 0) continue;  // 放置失败槽位（原语义保留）
        if (at(cx, cy).cityId < 0) addCity(1, cx, cy);
    }
    return true;
}

void Map::finalize() {
    // P13：城市数 = 注册表大小（城市实体数，非格子数）。基建格全在陆地（放置时校验），
    // 无海上城市 → 原清理步骤消失。旧经济公式 totalCities 语义随之变为"城市实体数比"。
}

int Map::addCity(int level, int baseX, int baseY) {
    const int idx = cityConfig_.levelIndex(level);
    if (idx < 0) return -1;
    const int w = cityConfig_.shapes[static_cast<size_t>(idx)][0];
    const int h = cityConfig_.shapes[static_cast<size_t>(idx)][1];
    City c;
    c.id = static_cast<int>(cities_.size());
    c.level = level;
    c.baseX = baseX;
    c.baseY = baseY;
    c.w = w;
    c.h = h;
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx)
            at(baseX + dx, baseY + dy).cityId = c.id;
    cities_.push_back(std::move(c));
    return c.id;
}

bool Map::canPlaceCity(int level, int baseX, int baseY) const {
    const int idx = cityConfig_.levelIndex(level);
    if (idx < 0) return false;
    const int w = cityConfig_.shapes[static_cast<size_t>(idx)][0];
    const int h = cityConfig_.shapes[static_cast<size_t>(idx)][1];
    if (baseX < 0 || baseY < 0 || baseX + w > width_ || baseY + h > height_) return false;
    if (!at(baseX, baseY).cityAllowed) return false;  // 锚点须可成城（基图允许该格成城）
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx) {
            const MapCell& c = at(baseX + dx, baseY + dy);
            if (!c.land) return false;          // 基建格全须陆地（含山）
            if (c.cityId != -1) return false;   // 无重叠
        }
    return true;
}

MapCell& Map::at(int x, int y) {
    return cells_[static_cast<size_t>(y) * width_ + x];
}

const MapCell& Map::at(int x, int y) const {
    return cells_[static_cast<size_t>(y) * width_ + x];
}

}  // namespace lw
