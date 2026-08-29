#include "world/Map.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>

#include "core/MathUtil.h"
#include "world/Bmp24.h"
#include "world/TerrainCodec.h"

namespace lw {

namespace {
// 首都重试上限：仅防病态地图死循环（正常地图远用不到，不影响确定性路径）。
constexpr int kCapitalMaxAttempts = 1000000;

// 概率 ramp：p = clamp((channel - probFloor) / probScale, 0, 1)。
// channel <= floor → 0；channel == floor+scale → 1（线性）。山用 R、城用 G 通道。
}  // namespace

double Map::sampleCityLevel(const Config::City& cc, const Config::City::TilingSet& set, Rng& rng) {
    // 等级采样（.docs/临时文本.txt 修正幂律）：
    //   S = 形状表展开的可重等级集合（同一级多个形状变体 = 多个元素，n_x 即重复次数）；
    //   P(x) ∝ n_x * (x + beta)^{-s}
    //   s = levelIncomeExponent + levelRankExponent（沿用旧配置 alpha+gamma）。
    //   beta = (1 - min(S)) / 2，min(S) 为当前密铺最小城市等级。
    // 用 unit() 而非 get()：unit() 是 virtual，测试可用 mock 固定分支。
    if (set.levels.empty()) return 1.0;
    std::vector<double> elems;
    elems.reserve(set.shapes.size());
    for (std::size_t si = 0; si < set.shapes.size(); ++si) {
        int li = (si < set.shapeLevelIndex.size())
                     ? set.shapeLevelIndex[si]
                     : static_cast<int>(std::min<std::size_t>(si, set.levels.size() - 1));
        if (li < 0 || li >= static_cast<int>(set.levels.size())) continue;
        elems.push_back(set.levels[static_cast<size_t>(li)]);
    }
    if (elems.empty()) return set.levels.front();
    const double minLevel = *std::min_element(elems.begin(), elems.end());
    const double beta = (1.0 - minLevel) * 0.5;
    const double alpha = cc.rankExponent() > 0.0 ? cc.rankExponent() : 1.5;
    double total = 0.0;
    std::vector<double> weights;
    weights.reserve(elems.size());
    for (double x : elems) {
        const double base = x + beta;
        if (base <= 0.0) {  // 数值防御：极低等级导致非正底数时按均匀处理
            weights.assign(elems.size(), 1.0);
            total = static_cast<double>(elems.size());
            break;
        }
        const double w = std::pow(base, -alpha);
        weights.push_back(w);
        total += w;
    }
    if (total <= 0.0) {
        const int n = static_cast<int>(elems.size());
        return elems[static_cast<size_t>(std::min(n - 1, static_cast<int>(rng.unit() * n)))];
    }
    double r = rng.unit() * total;
    for (std::size_t i = 0; i < elems.size(); ++i) {
        if (r < weights[i] || i + 1 == elems.size())
            return elems[i];
        r -= weights[i];
    }
    return elems.back();
}

void Map::clear() {
    cells_.assign(static_cast<size_t>(cellCount()), MapCell{});
    for (int idx = 0; idx < cellCount(); ++idx) {
        MapCell& c = cells_[static_cast<size_t>(idx)];
        c = MapCell{};
        // P12：格坐标语义 = (列, 行)。方：(x,y)；六：(c,r)；三：(i 对, r 行)；
        // 半正/Laves：表驱动 (r*cols+c)*B+b。
        int rr, cc, bb;
        geom_.indexToRowCol(idx, rr, cc, bb);
        c.x = cc;
        c.y = rr;
    }
    capitalX_.clear();
    capitalY_.clear();
    capitalB_.clear();
    cities_.clear();
}

void Map::configure(const Config::Map& cfg) {
    // P12/Phase 2：所有密铺统一先把用户长宽映射到周期域列/行；square 为恒等映射。
    // hex/tri 的映射参数同时保证垂直周期闭合所需的偶数行。
    int cols = cfg.width;
    int rows = cfg.height;
    chooseTableDomain(static_cast<int>(cfg.tilingType()), cfg.width, cfg.height, cols, rows);
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    width_ = cols;
    height_ = rows;
    capitalMinDistance_ = cfg.capitalMinDistance;
    geom_ = TilingGeom{cfg.tilingType(), cols, rows};
    clear();
}

bool Map::loadFromBmp(const std::string& path, Rng& rng) {
    Bmp24Image image;
    std::string error;
    if (!readBmp24(path, image, &error)) {
        spdlog::error("map file '{}': {}", path, error);
        return false;
    }
    if (image.width != width_ || image.height != height_) {
        spdlog::error("map file '{}': size mismatch ({}x{} vs {}x{})", path, image.width,
                      image.height, width_, height_);
        return false;
    }

    // P13 第一遍：读全图通道 → 确定海/陆（确定性，不掷骰）。城市形状占用后续格，须先全图
    // 海陆已知才能做放置检查（第二遍），故拆两遍。
    std::vector<CellChannels> ch(static_cast<size_t>(cellCount()));
    for (int j = 0; j < height_; ++j) {
        for (int i = 0; i < width_; ++i) {
            const auto& rgb = image.pixels[static_cast<size_t>(j) * width_ + i];
            const bool sea = terrain::isSea(rgb[0], rgb[1], rgb[2], terrain_);
            // P13 关键：第一遍即确定全图海陆（置 cell.land）。否则第二遍逐格时才设 land，
            // 多格形状要检查的"未来格"（同行右侧/下方行）land 仍是 clear 默认 false，
            // 放置恒失败回退 1 级（2026-08-07 修：测试暴露多格城市全部塌缩为 1 级）。
            at(i, j).land = !sea;
            ch[static_cast<size_t>(j) * width_ + i] = {sea, rgb[1], rgb[0]};
        }
    }

    finishTerrain(ch, rng);  // 第二遍：掷山/城骰 + 放置城市（方 = 原 y 先行后列序 ≡ 下标序）
    return true;
}

// P12：lwmap 加载（六/三角地形基图；自描述格式，见 MapGenerator）。通道语义与 BMP 一致
// （任一分量 < seaChannelMin → 海；山 R / 城 G 概率通道）→ 与 loadFromBmp 共用第二遍。
bool Map::loadFromLwmap(const std::string& path, Rng& rng) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        spdlog::error("lwmap file '{}' not found", path);
        return false;
    }
    char magic[4];
    ifs.read(magic, 4);
    if (ifs.gcount() != 4 || magic[0] != 'L' || magic[1] != 'W' || magic[2] != 'M' ||
        magic[3] != 'P') {
        spdlog::error("lwmap file '{}': bad magic", path);
        return false;
    }
    unsigned char ver = 0;
    ifs.read(reinterpret_cast<char*>(&ver), 1);
    if (ver != 1) {
        spdlog::error("lwmap file '{}': unsupported version {}", path, static_cast<int>(ver));
        return false;
    }
    unsigned char tilingByte = 0;
    ifs.read(reinterpret_cast<char*>(&tilingByte), 1);
    int cw = 0, rh = 0;
    ifs.read(reinterpret_cast<char*>(&cw), 4);
    ifs.read(reinterpret_cast<char*>(&rh), 4);
    if (ifs.eof() || cw <= 0 || rh <= 0 || cw > 100000 || rh > 100000) {
        spdlog::error("lwmap file '{}': bad header", path);
        return false;
    }
    // P12 旧格式：1=hex、2=tri。2026-08-16 起 tilingByte = TilingType 枚举值（0..16）。
    TilingType fileTiling = TilingType::Square;
    if (tilingByte < kTilingTypeCount)
        fileTiling = static_cast<TilingType>(tilingByte);
    else
        fileTiling = TilingType::Square;
    if (fileTiling != geom_.type) {
        spdlog::error("lwmap file '{}': tiling mismatch (file {}, map {})", path,
                      tilingName(fileTiling), tilingName(geom_.type));
        return false;
    }
    // 尺寸须与 configure 一致（几何由 Map 决定，文件只载通道）。
    if (cw != geom_.cols || rh != geom_.rows) {
        spdlog::error("lwmap file '{}': size mismatch ({}x{} vs {}x{})", path, cw, rh, geom_.cols,
                      geom_.rows);
        return false;
    }

    // 第一遍：读全图通道 → 海/陆（与 BMP 同语义：任一分量 < seaChannelMin → 海）。
    std::vector<CellChannels> ch(static_cast<size_t>(cellCount()));
    for (int idx = 0; idx < cellCount(); ++idx) {
        unsigned char bgr[3];
        ifs.read(reinterpret_cast<char*>(bgr), 3);
        if (ifs.gcount() != 3) {
            spdlog::error("lwmap file '{}': data truncated at cell {}", path, idx);
            return false;
        }
        const bool sea = terrain::isSea(bgr[2], bgr[1], bgr[0], terrain_);
        atIndex(idx).land = !sea;
        ch[static_cast<size_t>(idx)] = {sea, bgr[1], bgr[2]};
    }
    finishTerrain(ch, rng);  // 第二遍：掷山/城骰 + 放置城市（RNG 顺序 = 格下标序）
    return true;
}

// 第二遍（方/六/三/表驱动共用）：按随机顺序处理山地/建城许可；然后按城市密度与
// 幂律采样出目标等级列表，优先从大到小随机安置城市（见文件头注）。
void Map::finishTerrain(const std::vector<CellChannels>& ch, Rng& rng) {
    const int n = cellCount();

    // 随机处理顺序（Fisher-Yates，消耗 RNG 但确定性）。
    std::vector<int> order(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) order[static_cast<size_t>(i)] = i;
    for (int i = n - 1; i > 0; --i) {
        const int j = rng.get(i);
        std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(j)]);
    }

    // 1) 先逐格掷山/城许可，不再在循环里放城。
    for (int idx : order) {
        const CellChannels& cc = ch[static_cast<size_t>(idx)];
        MapCell& cell = atIndex(idx);
        cell.belongi = 0;
        cell.cityAllowed = false;
        cell.mountain = false;
        cell.cityId = -1;
        if (cc.sea) continue;
        cell.cityAllowed = terrain::ramp(cc.g, terrain_) > 0.0;
        cell.mountain = rng.chance(terrain::ramp(cc.r, terrain_));
    }
    correctMountainCoast();  // 邻海修正：山只出现在不与海相邻的陆地上

    // 2) 目标城市数 = Σ 陆地格建城概率（与 MapGenerator 的 cityDensity×陆格数一致）。
    const auto& set = cityConfig_.setFor(geom_.type);
    if (set.levels.empty()) return;
    double totalP = 0.0;
    std::vector<int> landCells;
    landCells.reserve(static_cast<size_t>(n));
    for (int idx = 0; idx < n; ++idx) {
        if (ch[static_cast<size_t>(idx)].sea) continue;
        totalP += terrain::ramp(ch[static_cast<size_t>(idx)].g, terrain_);
        if (atIndex(idx).cityAllowed) landCells.push_back(idx);
    }
    const int totalAttempts = static_cast<int>(std::lround(totalP));
    if (totalAttempts <= 0 || landCells.empty()) return;

    // 3) 计算本密铺与方形参考的幂律分布，按“总城市生产力相等”反解本密铺目标城市数。
    const double sExp = cityConfig_.rankExponent();
    const double alpha = cityConfig_.levelIncomeExponent;
    struct LevelInfo {
        double level;
        double weight;    // n_x * (level+beta)^-s
        double prod;      // weight * level^alpha
    };
    auto levelInfos = [&](const Config::City::TilingSet& tset) {
        std::vector<double> elems;
        for (std::size_t si = 0; si < tset.shapes.size(); ++si) {
            int li = (si < tset.shapeLevelIndex.size())
                         ? tset.shapeLevelIndex[si]
                         : static_cast<int>(std::min<std::size_t>(si, tset.levels.size() - 1));
            if (li >= 0 && li < static_cast<int>(tset.levels.size()))
                elems.push_back(tset.levels[static_cast<size_t>(li)]);
        }
        std::map<double, LevelInfo> info;
        if (elems.empty()) return info;
        const double minL = *std::min_element(elems.begin(), elems.end());
        const double beta = (1.0 - minL) * 0.5;
        for (double x : elems) {
            const double base = std::max(x + beta, 1e-9);
            LevelInfo& inf = info[x];
            inf.level = x;
            inf.weight += std::pow(base, -sExp);
        }
        double totalW = 0.0;
        for (auto& [lv, inf] : info) {
            inf.prod = inf.weight * std::pow(std::max(lv, 0.0), alpha);
            totalW += inf.weight;
        }
        for (auto& [lv, inf] : info) {
            inf.weight /= totalW;
            inf.prod /= totalW;
        }
        return info;
    };
    auto info = levelInfos(set);
    if (info.empty()) return;
    const auto squareInfo = levelInfos(cityConfig_.square);
    double eTiling = 0.0;
    double eSquare = 0.0;
    for (const auto& [lv, inf] : info) eTiling += inf.prod;
    for (const auto& [lv, inf] : squareInfo) eSquare += inf.prod;
    const double norm = (eTiling > 1e-9) ? (eSquare / eTiling) : 1.0;
    const int baseTarget = std::clamp(totalAttempts, 0, static_cast<int>(landCells.size()));
    int targetCities = static_cast<int>(std::lround(static_cast<double>(baseTarget) * norm));
    targetCities = std::clamp(targetCities, 0, static_cast<int>(landCells.size()));
    if (targetCities <= 0) return;

    // 4) 按归一化后的幂律概率分配各级城市数量（最大余数法），并从大到小排序。
    std::vector<double> levels;
    levels.reserve(static_cast<size_t>(targetCities));
    {
        struct Alloc {
            double level;
            int count;
            double rem;
        };
        std::vector<Alloc> allocs;
        allocs.reserve(info.size());
        int assigned = 0;
        for (const auto& [lv, inf] : info) {
            const double expected = static_cast<double>(targetCities) * inf.weight;
            const int base = static_cast<int>(std::floor(expected));
            allocs.push_back({lv, base, expected - static_cast<double>(base)});
            assigned += base;
        }
        std::sort(allocs.begin(), allocs.end(),
                  [](const Alloc& a, const Alloc& b) { return a.rem > b.rem; });
        const int extra = std::min(targetCities - assigned, static_cast<int>(allocs.size()));
        for (int i = 0; i < extra; ++i) ++allocs[static_cast<size_t>(i)].count;
        for (const Alloc& a : allocs)
            levels.insert(levels.end(), static_cast<size_t>(a.count), a.level);
    }

    std::sort(levels.begin(), levels.end(), std::greater<double>());

    // 5) 随机安置：对每个等级，先在可用锚点中随机尝试若干次；仍失败则扫描全部可用锚点。
    constexpr int kRandomTries = 200;
    for (double level : levels) {
        bool placed = false;
        const int candidateCount = static_cast<int>(landCells.size());
        for (int t = 0; t < kRandomTries && t < candidateCount; ++t) {
            const int idx = landCells[static_cast<size_t>(rng.get(candidateCount - 1))];
            if (atIndex(idx).cityId != -1) continue;
            if (!atIndex(idx).cityAllowed) continue;
            if (canPlaceCity(level, idx)) {
                addCity(level, idx, &rng);
                placed = true;
                break;
            }
        }
        if (placed) continue;
        for (int idx : landCells) {
            if (atIndex(idx).cityId != -1) continue;
            if (!atIndex(idx).cityAllowed) continue;
            if (canPlaceCity(level, idx)) {
                addCity(level, idx, &rng);
                placed = true;
                break;
            }
        }
        // 放不下就丢弃该次采样（保持目标等级列表中的大城优先；小城稍后仍有自己的采样）。
    }
}

void Map::correctMountainCoast() {
    if (geom_.type == TilingType::Square) {
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
        return;
    }
    // P12 六/三角/表驱动：共享顶点的海格也算邻海，山置普通陆（决策 4）。
    for (int idx = 0; idx < cellCount(); ++idx) {
        MapCell& c = atIndex(idx);
        if (!c.mountain) continue;
        bool nearSea = false;
        for (int k = 0; k < geom_.pointNeighborCount(idx) && !nearSea; ++k) {
            const int nb = geom_.pointNeighbor(idx, k);
            if (nb >= 0 && !atIndex(nb).land) nearSea = true;
        }
        if (nearSea) c.mountain = false;
    }
}

bool Map::placeCapitals(Rng& rng) {
    capitalX_.assign(static_cast<size_t>(kPlayerFactionCount), -9961);
    capitalY_.assign(static_cast<size_t>(kPlayerFactionCount), -9961);
    capitalB_.assign(static_cast<size_t>(kPlayerFactionCount), 0);
    const bool table = static_cast<int>(geom_.type) > static_cast<int>(TilingType::Tri);

    if (table) {
        // 半正/Laves：首都从已生成城市中分配（用户 2026-08-16：先生成可用城市，再分配首都），
        // 不再随机挑格后硬塞最小等级城；已生成城市不足 8 个时才回退到"合法最小等级城"。
        const auto& citySet = cityConfig_.setFor(geom_.type);
        const double minLevel = citySet.levels.empty() ? 1.0 : citySet.levels.front();
        std::vector<int> candAnchors;
        candAnchors.reserve(cities_.size());
        for (const auto& c : cities_) {
            if (c.baseIndex < 0 || c.baseIndex >= cellCount()) continue;
            if (!atIndex(c.baseIndex).land) continue;
            // 首都锚点必须是该城**实际占格**：Laves4612 等形状的锚点格 baseIndex 可能
            // 不属于形状格（L2/L4 变体不含 (0,0) 锚）→ atIndex(baseIndex).cityId == -1，
            // 首都落在该格会被判作"非城市格"（用户反馈的放置 bug）。改用该城任一带 cityId
            // 的占格；形状无标记格时退回锚点格。
            int hit = -1;
            for (int cell : cityCells(c))
                if (cell >= 0 && atIndex(cell).cityId == c.id) { hit = cell; break; }
            candAnchors.push_back(hit >= 0 ? hit : c.baseIndex);
        }
        std::vector<bool> used(candAnchors.size(), false);
        const int nCityCand = static_cast<int>(candAnchors.size());

        const auto distFromPlaced = [&](int idx) {
            double best = 1e18;
            double ax, ay;
            cellCenter(idx, ax, ay);
            for (int j = 0; j < kPlayerFactionCount; ++j) {
                if (capitalX_[static_cast<size_t>(j)] < 0) continue;
                const int pj = cellIndexAt(capitalX_[static_cast<size_t>(j)],
                                           capitalY_[static_cast<size_t>(j)],
                                           capitalB_[static_cast<size_t>(j)]);
                double bx, by;
                cellCenter(pj, bx, by);
                best = std::min(best, math::distance(ax, ay, bx, by));
            }
            return best;
        };
        int minDist = capitalMinDistance_;
        for (int i = 0; i < kPlayerFactionCount; ++i) {
            bool assigned = false;
            while (minDist >= 1) {
                int bestJ = -1;
                double bestD = -1.0;
                for (int j = 0; j < nCityCand; ++j) {
                    if (used[static_cast<size_t>(j)]) continue;
                    const double d = distFromPlaced(candAnchors[static_cast<size_t>(j)]);
                    if (d >= minDist && d > bestD) {
                        bestD = d;
                        bestJ = j;
                    }
                }
                if (bestJ >= 0) {
                    used[static_cast<size_t>(bestJ)] = true;
                    int rr = 0, cc = 0, bb = 0;
                    geom_.indexToRowCol(candAnchors[static_cast<size_t>(bestJ)], rr, cc, bb);
                    capitalX_[static_cast<size_t>(i)] = cc;
                    capitalY_[static_cast<size_t>(i)] = rr;
                    capitalB_[static_cast<size_t>(i)] = bb;
                    assigned = true;
                    break;
                }
                if (minDist <= 1) break;
                minDist = std::max(1, minDist / 2);
            }
            if (assigned) continue;
            // 回退：随机找一个合法锚点，放置最小等级城（canPlaceCity 会校验锚点边数/形状格）。
            bool ok = false;
            int attempts = 0;
            while (attempts < kCapitalMaxAttempts) {
                ++attempts;
                const int idx = rng.get(cellCount() - 1);
                const MapCell& cand = atIndex(idx);
                if (!cand.land || cand.cityId != -1) continue;
                // 回退阶段：已有城市不足 8 个 → 允许在任意陆地放最小等级城（形状/无重叠仍校验）。
                if (!canPlaceCityImpl(minLevel, idx, /*requireAllowed=*/false)) continue;
                if (distFromPlaced(idx) < minDist) continue;
                const int newId = addCity(minLevel, idx, &rng);
                if (newId < 0) continue;  // 放置失败（防御）：继续找下一锚点
                // 同上：首都锚点取新城的实际占格，避免锚点格不在形状内 → cityId==-1。
                int capCell = idx;
                for (int cell : cityCells(cities_[static_cast<size_t>(newId)]))
                    if (cell >= 0 && atIndex(cell).cityId == newId) { capCell = cell; break; }
                int rr = 0, cc = 0, bb = 0;
                geom_.indexToRowCol(capCell, rr, cc, bb);
                capitalX_[static_cast<size_t>(i)] = cc;
                capitalY_[static_cast<size_t>(i)] = rr;
                capitalB_[static_cast<size_t>(i)] = bb;
                ok = true;
                break;
            }
            if (!ok) {
                spdlog::error("placeCapitals: retry exhausted (table tiling, no valid city anchor)");
                return false;
            }
        }
        return true;
    }

    // ---- 方/六/三：保持原语义（基线/黄金数据路径）----
    int attempts = 0;
    // P5 改版：首都优先落在"可产城"格（cityAllowed = 基图允许该格成为城市，即源 g>probFloor）。
    // 这样基图"必然无城"的格子（ramp(G)=0）不会被首都强行塞进城市。
    // 每放一个首都前，先判断是否还有"可用可产城格"（未被占用且距已放首都足够远）：
    //   有 → 该首都只接受可产城格；无 → 放宽到任意陆地（保证 8 首都总能放下）。
    // P6 稀疏地图兜底（2026-08-06）：8 首都按全距离放不下（如 90% 海随机图）→ 距离降档
    //   （减半）重试；正常地图远用不到 → RNG 序列不变，黄金数据/基线不受影响。
    int minDist = capitalMinDistance_;
    // P12：首都间距按格心**世界距离**（六/三角偏移行下格坐标距离是扭曲的；方 = 格距）。
    const auto worldDist = [&](int x1, int y1, int x2, int y2) {
        if (geom_.type == TilingType::Square)
            return math::distance(static_cast<double>(x1), static_cast<double>(y1),
                                  static_cast<double>(x2), static_cast<double>(y2));
        double ax, ay, bx, by;
        cellCenter(cellIndexAt(x1, y1), ax, ay);
        cellCenter(cellIndexAt(x2, y2), bx, by);
        return math::distance(ax, ay, bx, by);
    };
    // 扫描"还有可用可产城格"（用给定 dist；minDist 降档时重扫）。
    const auto scanUsable = [&](int dist, int placedCount) -> bool {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const MapCell& c = atIndex(cellIndexAt(x, y));
                if (!c.land || !c.cityAllowed) continue;
                bool far = true;
                for (int j = 0; j < placedCount; ++j) {
                    if (worldDist(x, y, capitalX_[static_cast<size_t>(j)],
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
            const int candIdx = cellIndexAt(rx, ry);
            const MapCell& cand = atIndex(candIdx);
            if (!cand.land) continue;
            if (anyUsableCityCell && !cand.cityAllowed) continue;  // 还有可产城格可用 → 只收它
            bool tooClose = false;
            for (int j = 0; j < i; ++j) {
                if (worldDist(rx, ry, capitalX_[static_cast<size_t>(j)],
                              capitalY_[static_cast<size_t>(j)]) < minDist) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;
            capitalX_[static_cast<size_t>(i)] = rx;
            capitalY_[static_cast<size_t>(i)] = ry;
            capitalB_[static_cast<size_t>(i)] = 0;
            break;
        }
    }
    // P13：锚点格已是城市（loadFromBmp 放置的形状）→ 该城即首都（不新建）；否则注册 1 级城
    //（方/六/三最小等级均为 1，即文档存在的单格城）。
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        const int cx = capitalX_[static_cast<size_t>(i)];
        const int cy = capitalY_[static_cast<size_t>(i)];
        if (cx < 0) continue;  // 放置失败槽位（原语义保留）
        if (atIndex(cellIndexAt(cx, cy)).cityId < 0) addCity(1.0, cellIndexAt(cx, cy), &rng);
    }
    return true;
}

// 格坐标 (c, r) → 格下标（统一委托 TilingGeom；b=0 为正方/六边形唯一格或三角正格）。
int Map::cellIndexAt(int c, int r) const {
    return geom_.cellIndexAt(r, c, 0);
}

int Map::cellIndexAt(int c, int r, int b) const {
    return geom_.cellIndexAt(r, c, b);
}

// 形状 → 基建格下标（level 查密铺形状表；锚点 index 为形状原点；variant < 0 时按锚点
// 确定性选取）。P12。渲染/放置统一用 City::shapeVariant（放置时选定、快照持久化），
// 保证同级多形状（Laves/部分半正）的锚朝向/局部拓扑匹配不因 anchorIndex 取模漂移。
std::vector<int> Map::shapeCells(double level, int anchorIndex, int variant) const {
    std::vector<int> out;
    const auto& set = cityConfig_.setFor(geom_.type);
    const int vc = set.variantCount(level);
    int v = variant;
    if (v < 0) v = (vc > 1) ? (std::abs(anchorIndex) % vc) : 0;  // 旧确定性选变体（测试用）
    const Config::City::Shape* sh = set.shapeFor(level, v);
    if (!sh || anchorIndex < 0 || anchorIndex >= cellCount()) return out;
    out.reserve(sh->cells.size());
    return resolveShapeCells(*sh, anchorIndex);
}

// P1.2：统一形状解析。所有密铺 cells 都是相对锚格中心的世界偏移；
// 三角反锚时镜像 dy（形状表按正锚模式定义）。
std::vector<int> Map::resolveShapeCells(const Config::City::Shape& sh, int anchorIndex) const {
    std::vector<int> out;
    if (anchorIndex < 0 || anchorIndex >= cellCount()) return out;
    out.reserve(sh.cells.size());
    double ax, ay;
    cellCenter(anchorIndex, ax, ay);
    const bool anchorUp = (geom_.type == TilingType::Tri) ? ((anchorIndex & 1) == 0) : true;
    for (const auto& sc : sh.cells) {
        const double wx = ax + sc.dx;
        const double wy = ay + (anchorUp ? sc.dy : -sc.dy);
        out.push_back(geom_.worldToCell(wx, wy));
    }
    return out;
}

std::vector<int> Map::cityCells(const City& c) const { return shapeCells(c.level, c.baseIndex, c.shapeVariant); }

void Map::cityCenter(const City& c, double& wx, double& wy) const {
    const std::vector<int> cells = cityCells(c);
    double sx = 0.0, sy = 0.0;
    int n = 0;
    for (int idx : cells) {
        if (idx < 0) continue;
        double cx0, cy0;
        cellCenter(idx, cx0, cy0);
        sx += cx0;
        sy += cy0;
        ++n;
    }
    if (n > 0) {
        wx = sx / n;
        wy = sy / n;
    } else {
        wx = c.baseX + 0.5;
        wy = c.baseY + 0.5;
    }
}

// P12：重算全部城市的 baseIndex/几何中心（快照读档后调用；方 = baseX+w/2 同旧式）。
void Map::updateCityGeometry(City& c) {
        if (geom_.type == TilingType::Square) {
            c.baseIndex = c.baseY * geom_.cols + c.baseX;
            c.w = 1;
            c.h = 1;
            const Config::City::Shape* sh = cityConfig_.shapeFor(geom_.type, c.level);
            if (sh) {
                for (const auto& sc : sh->cells) {
                    c.w = std::max(c.w, static_cast<int>(sc.dx) + 1);
                    c.h = std::max(c.h, static_cast<int>(sc.dy) + 1);
                }
            }
        } else {
            // 六/三：baseX/baseY 仅供展示；baseIndex 为权威锚点——三角下标含奇偶（朝向
            // 正/反），快照已存（旧档缺失时 deserialize 已按正锚 cellIndexAt 推回），
            // 此处**不再重推**，避免丢失反锚朝向（旧实现强制 2*(r*cols+c) 抹掉奇偶）。
            if (geom_.type == TilingType::Hex) c.baseIndex = c.baseY * geom_.cols + c.baseX;
        }
        double cx0, cy0;
        cityCenter(c, cx0, cy0);
        c.centerX_ = cx0;
        c.centerY_ = cy0;
        // AABB（显示用；六/三取形状格中心的世界包围盒折算列/行）。
        const std::vector<int> cells = cityCells(c);
        if (!cells.empty() && geom_.type != TilingType::Square) {
            double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
            for (int idx : cells) {
                if (idx < 0) continue;
                double ccx, ccy;
                cellCenter(idx, ccx, ccy);
                minX = std::min(minX, ccx);
                maxX = std::max(maxX, ccx);
                minY = std::min(minY, ccy);
                maxY = std::max(maxY, ccy);
            }
            c.w = std::max(1, static_cast<int>(std::lround((maxX - minX) + 1.0)));
            c.h = std::max(1, static_cast<int>(std::lround((maxY - minY) + 1.0)));
        }
}

void Map::recomputeCityGeometry() {
    for (City& c : cities_) updateCityGeometry(c);
}

int Map::addCity(double level, int index, Rng* rng) {
    const auto& set = cityConfig_.setFor(geom_.type);
    const int vc = set.variantCount(level);
    // 同级多形状：选"能放下"的变体（与 canPlaceCityImpl 判定一致），保证锚朝向/局部
    // 拓扑匹配；持久化到 City::shapeVariant，渲染/读档用同一变体（2026-08-16 修复：
    // 旧逻辑 abs(index)%vc 盲选，会选中放不下的变体导致城市形状错乱/重叠）。
    // 2026-08-17：rng 非空且变体>1 时**随机**选一个能放下的变体——否则固定选第一个
    // 能放下的，同级其他形状永不出现（如 Laves31212 的菱形 6 级城、Arch31212 正反）。
    int variant = 0;
    if (vc > 1) {
        const std::vector<int> vs = placeableVariants(level, index, /*requireAllowed=*/true);
        if (!vs.empty()) {
            variant = (rng != nullptr) ? vs[static_cast<size_t>(rng->get(static_cast<int>(vs.size()) - 1))]
                                       : vs.front();
        } else {
            variant = 0;  // 调用方已 canPlaceCity 保证可放，防御兜底
        }
    }
    const Config::City::Shape* sh = set.shapeFor(level, variant);
    if (!sh || index < 0 || index >= cellCount()) return -1;
    const int id = static_cast<int>(cities_.size());
    City c;
    c.id = id;
    c.level = level;
    c.area = level;
    c.baseIndex = index;
    c.shapeVariant = variant;
    if (geom_.type == TilingType::Square) {
        c.baseX = index % geom_.cols;
        c.baseY = index / geom_.cols;
    } else {
        c.baseX = index % geom_.cols;
        c.baseY = index / geom_.cols;
    }
    const std::vector<int> cells = shapeCells(level, index, variant);
    for (int idx : cells)
        if (idx >= 0) atIndex(idx).cityId = id;
    // 几何中心/AABB 由新城与读档共用同一套规则。
    City& city = cities_.emplace_back(std::move(c));
    updateCityGeometry(city);
    return id;
}

bool Map::canPlaceCity(double level, int index) const {
    return canPlaceCityImpl(level, index, /*requireAllowed=*/true);
}

bool Map::canPlaceCityImpl(double level, int index, bool requireAllowed) const {
    return placeableVariant(level, index, requireAllowed) >= 0;
}

int Map::placeableVariant(double level, int index, bool requireAllowed) const {
    const std::vector<int> vs = placeableVariants(level, index, requireAllowed);
    return vs.empty() ? -1 : vs.front();
}

std::vector<int> Map::placeableVariants(double level, int index, bool requireAllowed) const {
    std::vector<int> out;
    if (index < 0 || index >= cellCount()) return out;
    if (requireAllowed && !atIndex(index).cityAllowed)
        return out;  // 锚点须可成城（基图允许该格成城）
    const auto& set = cityConfig_.setFor(geom_.type);
    const int vc = set.variantCount(level);
    if (vc <= 0) return out;  // 未注册等级 → 拒绝（旧 levelIndex<0 语义）
    for (int v = 0; v < vc; ++v) {
        const Config::City::Shape* sh = set.shapeFor(level, v);
        if (!sh || sh->cells.empty()) continue;
        // 锚点基础格限制（如 Arch3464 L1/L2 仅限横平竖直正方形；由 baseGroups/anchorBases 定义）。
        if (sh->anchorBaseMask != 0) {
            const int b = index % std::max(1, geom_.baseCount());
            if (b < 0 || b >= 31 || (sh->anchorBaseMask & (1u << b)) == 0) continue;
        }
        // 用统一解析器逐变体解析形状格（shapeCells 也走同一 helper）。
        const std::vector<int> cells = resolveShapeCells(*sh, index);
        bool ok = true;
        for (int idx : cells) {
            if (idx < 0) { ok = false; break; }  // 界内（含环绕图不跨接缝）
            const MapCell& c = atIndex(idx);
            if (!c.land || c.cityId != -1) { ok = false; break; }  // 陆地且无重叠
        }
        if (ok) out.push_back(v);
    }
    return out;
}

MapCell& Map::atIndex(int idx) {
    return cells_[static_cast<size_t>(idx)];
}

const MapCell& Map::atIndex(int idx) const {
    return cells_[static_cast<size_t>(idx)];
}

MapCell& Map::at(int x, int y) {
    return cells_[static_cast<size_t>(y) * width_ + x];
}

const MapCell& Map::at(int x, int y) const {
    return cells_[static_cast<size_t>(y) * width_ + x];
}

}  // namespace lw
