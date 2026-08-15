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

double Map::sampleCityLevel(const Config::City& cc, const Config::City::TilingSet& set, Rng& rng) {
    // P13：等级采样（幂律分布，思路 9.1）。s = alpha + beta（config），
    //   P(L=levels[i]) = levels[i]^{-s} - levels[i+1]^{-s}（末级 = levels[N-1]^{-s}）。
    // u = rng.unit() 按累计概率确定等级（1 次 RNG）。levels 须升序。
    // 2026-08-16：levels 为 double（半正=面积和；Laves=格数）；最小等级可以不是 1，
    //   公式对非 1 起点自然成立（条件分布）。
    if (set.levels.empty()) return 1.0;
    const double s = cc.rankExponent();
    const double u = rng.unit();
    double cum = 0.0;
    for (int i = 0; i < static_cast<int>(set.levels.size()); ++i) {
        const double cur = std::pow(set.levels[static_cast<size_t>(i)], -s);
        const double next = (i + 1 < static_cast<int>(set.levels.size()))
                                ? std::pow(set.levels[static_cast<size_t>(i + 1)], -s)
                                : 0.0;
        cum += cur - next;  // P(L=levels[i]) = levels[i]^-s - levels[i+1]^-s
        if (u < cum) return set.levels[static_cast<size_t>(i)];
    }
    return set.levels.back();  // 浮点兜底（累计 ≈ 1）
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
    cities_.clear();
}

void Map::configure(const Config::Map& cfg) {
    // P12：密铺几何。六/三角行数 clamp 到偶数（垂直环绕闭合必需，见 P12 §2.3）；
    // 三角列数减半（width 语义 = 视觉列数，列对数 = width/2）并强制偶，与
    // MapGenerator::generate 同一公式（否则 lwmap 头尺寸不匹配）；width_/height_ 统一为
    // clamp 后值（width() == geom_.cols、height() == geom_.rows，首都采样等使用点一致）。
    int cols = cfg.width;
    if (cfg.tilingType() == TilingType::Tri) cols = (cols / 2) & ~1;
    int rows = cfg.height;
    // 六/三角行数须为偶数（垂直环绕闭合）；半正/Laves 表驱动用矩形周期域，任意行数均可。
    if ((cfg.tilingType() == TilingType::Hex || cfg.tilingType() == TilingType::Tri)
        && (rows & 1))
        --rows;
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    width_ = cols;
    height_ = rows;
    blockSize_ = cfg.blockSize;
    panelWidth_ = cfg.panelWidth;
    capitalMinDistance_ = cfg.capitalMinDistance;
    geom_ = TilingGeom{cfg.tilingType(), cols, rows};
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
    std::vector<CellChannels> ch(static_cast<size_t>(cellCount()));
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
        const bool sea = std::min({bgr[2], bgr[1], bgr[0]}) < terrain_.seaChannelMin;
        atIndex(idx).land = !sea;
        ch[static_cast<size_t>(idx)] = {sea, bgr[1], bgr[2]};
    }
    finishTerrain(ch, rng);  // 第二遍：掷山/城骰 + 放置城市（RNG 顺序 = 格下标序）
    return true;
}

// 第二遍（方/六/三共用）：按格下标序掷山/城骰 + 放置城市（见文件头注）。
void Map::finishTerrain(const std::vector<CellChannels>& ch, Rng& rng) {
    for (int idx = 0; idx < cellCount(); ++idx) {
        const CellChannels& cc = ch[static_cast<size_t>(idx)];
        MapCell& cell = atIndex(idx);
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
            const double level = sampleCityLevel(cityConfig_, cityConfig_.setFor(geom_.type), rng);
            if (canPlaceCity(level, idx)) {
                addCity(level, idx);
            } else {
                // 放置失败回退到该密铺的最小允许等级（2026-08-16：原硬编码 1 级，
                // 最小等级 ≠ 1 时 shapeFor(1) 不存在会丢失城市）。
                const auto& set = cityConfig_.setFor(geom_.type);
                const double minLevel = set.levels.empty() ? 1.0 : set.levels.front();
                if (canPlaceCity(minLevel, idx)) addCity(minLevel, idx);
            }
        }
        cell.mountain = rng.chance(rampProb(cc.r, terrain_));
    }
    correctMountainCoast();  // 邻海修正：山只出现在不与海相邻的陆地上
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
    // P12 六/三角："距海 ≤1 格" = 边邻有海 → 山置普通陆（决策 4；边邻语义）。
    for (int idx = 0; idx < cellCount(); ++idx) {
        MapCell& c = atIndex(idx);
        if (!c.mountain) continue;
        bool nearSea = false;
        for (int k = 0; k < geom_.neighborCount() && !nearSea; ++k) {
            const int nb = geom_.neighbor(idx, k);
            if (nb >= 0 && !atIndex(nb).land) nearSea = true;
        }
        if (nearSea) c.mountain = false;
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
            break;
        }
    }
    // P13：锚点格已是城市（loadFromBmp 放置的形状）→ 该城即首都（不新建）；否则注册最小等级城
    //（2026-08-16：原硬编码 1 级，最小等级 ≠ 1 时应注册该密铺的最小允许等级）。
    const auto& citySet = cityConfig_.setFor(geom_.type);
    const double minLevel = citySet.levels.empty() ? 1.0 : citySet.levels.front();
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        const int cx = capitalX_[static_cast<size_t>(i)];
        const int cy = capitalY_[static_cast<size_t>(i)];
        if (cx < 0) continue;  // 放置失败槽位（原语义保留）
        if (atIndex(cellIndexAt(cx, cy)).cityId < 0) addCity(minLevel, cellIndexAt(cx, cy));
    }
    return true;
}

// 格坐标 (c, r) → 格下标（P12；方 = r*width+c；六 = r*cols+c；三 = 正三角锚 2*(r*cols+c)；
// 半正/Laves = (r*cols+c)*B（基础格 0））。
int Map::cellIndexAt(int c, int r) const {
    if (geom_.type == TilingType::Tri) return 2 * (r * geom_.cols + c);
    if (static_cast<int>(geom_.type) > static_cast<int>(TilingType::Tri))
        return (r * geom_.cols + c) * geom_.baseCount();
    return r * geom_.cols + c;
}

void Map::finalize() {
    // P13：城市数 = 注册表大小（城市实体数，非格子数）。基建格全在陆地（放置时校验），
    // 无海上城市 → 原清理步骤消失。旧经济公式 totalCities 语义随之变为"城市实体数比"。
}

// 形状 → 基建格下标（level 查密铺形状表；锚点 index 为形状原点）。P12。
std::vector<int> Map::shapeCells(double level, int anchorIndex) const {
    std::vector<int> out;
    const Config::City::Shape* sh = cityConfig_.shapeFor(geom_.type, level);
    if (!sh || anchorIndex < 0 || anchorIndex >= cellCount()) return out;
    out.reserve(sh->cells.size());
    if (geom_.type == TilingType::Square) {
        int ax, ay;
        if (geom_.cols > 0) {
            ax = anchorIndex % geom_.cols;
            ay = anchorIndex / geom_.cols;
        } else {
            ax = ay = 0;
        }
        for (const auto& sc : sh->cells) {
            // 界内校验（旧 canPlaceCity 的 baseX+w>width 语义）：越界格 → -1。
            const int x = ax + static_cast<int>(sc.dx);
            const int y = ay + static_cast<int>(sc.dy);
            if (x < 0 || x >= geom_.cols || y < 0 || y >= geom_.rows) {
                out.push_back(-1);
                continue;
            }
            out.push_back(y * geom_.cols + x);
        }
    } else {
        double ax, ay;
        cellCenter(anchorIndex, ax, ay);
        // 三角：锚朝向决定形状变体（用户 2026-08 定稿：L1/L4 有正/反两种模式，其余同形）。
        // 形状表按"正锚模式"定义；锚为反三角（奇下标）时对 dy 垂直镜像（orient 随之翻转），
        // 偏移点即目标格中心 → worldToCell 稳定解析。六边形（轴向偏移）与锚朝向无关。
        // 半正/Laves：形状表按"目标格中心 − 锚格中心"的世界偏移存储（平移不变）→ 直接
        // 加锚中心后 worldToCell。
        const bool anchorUp = (geom_.type == TilingType::Tri) ? ((anchorIndex & 1) == 0) : true;
        for (const auto& sc : sh->cells) {
            double wx, wy;
            if (geom_.type == TilingType::Hex) {
                wx = ax + TilingGeom::kHexColSpacing * (sc.dx + 0.5 * sc.dy);
                wy = ay + TilingGeom::kHexRowSpacing * sc.dy;
            } else if (geom_.type == TilingType::Tri) {
                wx = ax + sc.dx * TilingGeom::kTriSide;
                wy = ay + (anchorUp ? sc.dy : -sc.dy) * TilingGeom::kTriAlt;
            } else {  // 半正/Laves：世界偏移
                wx = ax + sc.dx;
                wy = ay + sc.dy;
            }
            out.push_back(geom_.worldToCell(wx, wy));
        }
    }
    return out;
}

std::vector<int> Map::cityCells(const City& c) const { return shapeCells(c.level, c.baseIndex); }

void Map::cityCenter(const City& c, double& wx, double& wy) const {
    const std::vector<int> cells = shapeCells(c.level, c.baseIndex);
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
void Map::recomputeCityGeometry() {
    for (City& c : cities_) {
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
        const std::vector<int> cells = shapeCells(c.level, c.baseIndex);
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
}

int Map::addCity(double level, int index) {
    const Config::City::Shape* sh = cityConfig_.shapeFor(geom_.type, level);
    if (!sh || index < 0 || index >= cellCount()) return -1;
    const int id = static_cast<int>(cities_.size());
    City c;
    c.id = id;
    c.level = level;
    c.baseIndex = index;
    if (geom_.type == TilingType::Square) {
        c.baseX = index % geom_.cols;
        c.baseY = index / geom_.cols;
    } else {
        c.baseX = index % geom_.cols;
        c.baseY = index / geom_.cols;
    }
    const std::vector<int> cells = shapeCells(level, index);
    for (int idx : cells)
        if (idx >= 0) atIndex(idx).cityId = id;
    // 几何中心 + AABB（显示用：方 = w×h 形状；六/三 = 形状格中心世界包围盒折算列/行）。
    City& city = cities_.emplace_back(std::move(c));
    double cx0, cy0;
    cityCenter(city, cx0, cy0);
    city.centerX_ = cx0;
    city.centerY_ = cy0;
    if (geom_.type == TilingType::Square) {
        int w = 1, h = 1;
        for (const auto& sc : sh->cells) {
            w = std::max(w, static_cast<int>(sc.dx) + 1);
            h = std::max(h, static_cast<int>(sc.dy) + 1);
        }
        city.w = w;
        city.h = h;
    } else {
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
        city.w = std::max(1, static_cast<int>(std::lround(maxX - minX + 1.0)));
        city.h = std::max(1, static_cast<int>(std::lround(maxY - minY + 1.0)));
    }
    return id;
}

bool Map::canPlaceCity(double level, int index) const {
    const Config::City::Shape* sh = cityConfig_.shapeFor(geom_.type, level);
    if (!sh || sh->cells.empty()) return false;  // 未注册等级 → 拒绝（旧 levelIndex<0 语义）
    if (index < 0 || index >= cellCount()) return false;
    if (!atIndex(index).cityAllowed) return false;  // 锚点须可成城（基图允许该格成城）
    // 半正/Laves 混合面：形状可限定锚点格类型（按边数 = 多边形顶点数）。
    if (sh->anchorN != 0 && geom_.neighborCount(index) != sh->anchorN) return false;
    const std::vector<int> cells = shapeCells(level, index);
    for (int idx : cells) {
        if (idx < 0) return false;  // 界内（含环绕图不跨接缝）
        const MapCell& c = atIndex(idx);
        if (!c.land) return false;          // 基建格全须陆地（含山）
        if (c.cityId != -1) return false;   // 无重叠
    }
    return true;
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
