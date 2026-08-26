// CityRenderer.cpp — P13 城市渲染实现（render/CityRenderer）。
// 2026-08-07 用户反馈：去除 data/city.png 基建格贴图（城市只由等级塔图标 + 高缩放细线表示）；
// 细线颜色 = 势力色加深（render.city.lineDarken），与城市图标同势力色处理。
#include "render/CityRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/GameDefs.h"
#include "render/RendererUtil.h"
#include "world/tiling/CityIconFitter.h"

namespace lw::render {

namespace {
constexpr int kTerrainTexSize = 128;  // 塔贴图 LOD 档位基准（源图尺寸可小于此 → 档位被忽略）
constexpr int kTowerCount = 9;        // 等级塔贴图数（等级 1..9）
constexpr unsigned kBlack = 0x000000u;  // mix 用黑（细线加深 = 势力色 × lineDarken）

// 等级塔源图路径（data/tower/tower<N>.png；文件名编号即等级）。
std::string towerPath(int level) {
    return "data/tower/tower" + std::to_string(level) + ".png";
}

// 城市等级（实数）→ 贴图等级（1..10）。四舍五入；10 级及以上统一用 tower10 贴图。
int textureLevelFor(double level) {
    if (level >= 10.0) return 10;
    return std::clamp(static_cast<int>(std::lround(level)), 1, 10);
}

// 首都图标源图路径（P15：data/tower/capital.png，白色前景透明背景，与其他城市贴图同类）。
std::string capitalPath() { return "data/tower/capital.png"; }

// 绘制一批图标（等级塔：towers_[level-1]；首都/候补：capital_）。alpha 施加于纹理（虚化候补）。
// 共用 TintCache::pickSizeIndex LOD 选档 + 按该档实际纹理尺寸采样（塔图非方形，2026-08-07 偏移修复）。
void drawIcons(const std::vector<CityRenderer::Icon>& icons,
               const std::array<TintCache, 9>& towers, const TintCache& tower10,
               const TintCache& capital, bool useCapitalTex, Renderer& r, int alpha) {
    for (const auto& ic : icons) {
        // 10 级及以上统一 tower10 贴图：等级塔数组 towers_ 只装 1..9（下标 = level-1），
        // arch_33434 的 iconLevels 含 12 → texLevel=12 直接 towers_[11] 会越界 → 闪退（修 2026-08）。
        const TintCache& cache = useCapitalTex ? capital
                                  : (ic.level >= 10) ? tower10
                                                     : towers[static_cast<size_t>(ic.level - 1)];
        if (cache.count() == 0) continue;
        const int ci = std::clamp(ic.colorIndex, 0, cache.count() - 1);
        const int si = cache.pickSizeIndex(ic.dstW);
        SDL_Texture* tex = cache.texture(ci, si);
        if (!tex) continue;
        const SDL_Rect src{0, 0, cache.width(si), cache.height(si)};
        r.drawSpriteCenteredRect(tex, src, ic.cx, ic.cy, ic.dstW, ic.dstH, alpha);
    }
}

// 绘制一批细线（普通城市用 city.lineThickness、正式首都用 capital.lineThickness）。
// 颜色 = 归属势力色 × darken（与图标同势力色处理但加深；mix 黑 → col×rate）。
void drawOutlines(const std::vector<CityRenderer::Outline>& outlines, double thickness,
                  double darken, const std::vector<std::array<int, 3>>& factionColors,
                  Renderer& r) {
    if (thickness <= 0.0 || outlines.empty() || factionColors.empty()) return;
    const int thick = std::max(1, static_cast<int>(thickness + 0.5));
    for (const auto& o : outlines) {
        if (o.w <= 0 || o.h <= 0) continue;
        const std::size_t ci = std::clamp(o.colorIndex, 0,
                                          static_cast<int>(factionColors.size()) - 1);
        const SDL_Color lineCol = Renderer::mixed(factionColors[ci], kBlack, darken, 180);
        r.fillRect(o.x, o.y, o.w, thick, lineCol);                 // 上边
        r.fillRect(o.x, o.y + o.h - thick, o.w, thick, lineCol);   // 下边
        r.fillRect(o.x, o.y, thick, o.h, lineCol);                 // 左边
        r.fillRect(o.x + o.w - thick, o.y, thick, o.h, lineCol);   // 右边
    }
}

// P12：六/三角基建地块边界线段（形状外廓）。颜色 = 归属势力色 × darken，**不透明**
//（半透明线盖在邻势力格上会形成"双势力混色"观感，2026-08 反馈修复）；按 thickness 粗线。
void drawHullSegs(const std::vector<CityRenderer::Hull>& hulls, double thickness, double darken,
                  const std::vector<std::array<int, 3>>& factionColors, Renderer& r) {
    if (thickness <= 0.0 || hulls.empty() || factionColors.empty()) return;
    const int thick = std::max(1, static_cast<int>(thickness + 0.5));
    for (const auto& h : hulls) {
        if (h.segs.empty()) continue;
        const std::size_t ci = std::clamp(h.colorIndex, 0,
                                          static_cast<int>(factionColors.size()) - 1);
        const SDL_Color lineCol = Renderer::mixed(factionColors[ci], kBlack, darken, 255);
        for (const auto& s : h.segs)
            r.fillThickSegment(s[0], s[1], s[2], s[3], thick, lineCol);
    }
}
}  // namespace

void CityRenderer::bake(const Config& cfg, double iconDarken) {
    // 留存主色表（细线加深查表用；双色系统：细线 = 主色 × lineDarken）。
    factionColors_.clear();
    factionColors_.reserve(cfg.factions.size());
    for (const auto& f : cfg.factions) factionColors_.push_back(f.color);
    // 等级塔 + 首都图标：白底透明线条源图 → 目标色（Multiply，与玩家指示/山同管线）。
    // 双色系统（视觉工程改进 ⑫）：图标色 = cfg.factionCityColor(id)（主:副:黑 加权）× iconDarken
    //（render.city.iconDarken），避免亮色图标在亮陆地上对比弱（2026-08-07 反馈调暗）。
    // 细线仍用主色 × lineDarken（draw 里单独混黑）。
    std::vector<std::array<int, 3>> iconCols;
    iconCols.reserve(cfg.factions.size());
    for (std::size_t id = 0; id < cfg.factions.size(); ++id)
        iconCols.push_back(scaledColor(cfg.factionCityColor(static_cast<int>(id)), iconDarken));
    const std::vector<int> lodSizes = {kTerrainTexSize, 64, 32, 16};
    for (int lv = 1; lv <= kTowerCount; ++lv) {
        TintCache& cache = towers_[static_cast<size_t>(lv - 1)];
        cache.load(ren_, towerPath(lv), iconCols, TintMode::Multiply,
                   /*grayscaleFirst=*/false, /*preserveWhite=*/0, lodSizes);
        // 记录源纵横比（高/宽；图标框适配用）。源宽 0（加载失败）→ 保持 0 → 按 1.0 处理。
        if (cache.width() > 0)
            srcAspect_[static_cast<size_t>(lv - 1)] =
                static_cast<double>(cache.height()) / static_cast<double>(cache.width());
    }
    // 10 级及以上统一贴图（data/tower/tower10.png，新贴图未就绪时由用户提供）。
    tower10_.load(ren_, towerPath(10), iconCols, TintMode::Multiply,
                  /*grayscaleFirst=*/false, /*preserveWhite=*/0, lodSizes);
    if (tower10_.width() > 0)
        tower10Aspect_ = static_cast<double>(tower10_.height()) / static_cast<double>(tower10_.width());
    // P15：首都图标贴图（data/tower/capital.png，同塔管线）。
    capital_.load(ren_, capitalPath(), iconCols, TintMode::Multiply,
                  /*grayscaleFirst=*/false, /*preserveWhite=*/0, lodSizes);
    if (capital_.width() > 0)
        capitalAspect_ = static_cast<double>(capital_.height()) / static_cast<double>(capital_.width());
}

void CityRenderer::setTowerSourceSize(int level, int srcW, int srcH) {
    if (srcW <= 0 || srcH <= 0) return;
    if (level == 10) {
        tower10Aspect_ = static_cast<double>(srcH) / static_cast<double>(srcW);
        return;
    }
    if (level < 1 || level > kTowerCount) return;
    srcAspect_[static_cast<size_t>(level - 1)] =
        static_cast<double>(srcH) / static_cast<double>(srcW);
}

void CityRenderer::setCapitalSourceSize(int srcW, int srcH) {
    if (srcW <= 0 || srcH <= 0) return;
    capitalAspect_ = static_cast<double>(srcH) / static_cast<double>(srcW);
}

CityRenderer::Frame CityRenderer::compute(const Map& map, const Config::Render& rc,
                                          const std::vector<int>& capitalStatus) const {
    Frame f;
    const bool highZoom = cam_.cellPx() > rc.city.lineMinCellPx;
    const int nColor = kFactionTotal;  // 势力色数（含中立 0）
    // capitalStatus[c.id]：0=普通、1=正式首都、2=候补指定。空向量 → 全普通（向后兼容）。
    const auto statusOf = [&](const City& c) -> int {
        if (c.id < 0 || static_cast<size_t>(c.id) >= capitalStatus.size()) return 0;
        return capitalStatus[static_cast<size_t>(c.id)];
    };
    for (const City& c : map.cities()) {
        // 等级塔/首都图标缩放表按贴图等级索引（1..9；实数等级四舍五入，10+ 统一贴图）。
        const int texLevel = map.cityConfig().setFor(map.tiling()).iconLevelFor(c.level);
        if (texLevel < 1) continue;  // 防御：非法等级
        const int status = statusOf(c);
        const bool isCapital = (status == 1 || status == 2);  // 正式首都 / 候补（都用首都图标）

        const bool tiled = map.tiling() != TilingType::Square;
        SDL_Rect block{0, 0, 0, 0};
        // 形状占格（P12 六/三/半正/Laves）：本城形状格集合。整城只解析一次（存本城市）。
        // 供视野剔除 AABB、高缩放外廓细线、图标拟合兜底三方共用（旧代码每处重复 cityCells）。
        std::vector<int> cells;
        if (tiled) {
            // P12 六/三角：视野剔除用形状世界 AABB（屏幕）。
            cells = map.cityCells(c);
            double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
            double wx[12], wy[12];
            for (int ci : cells) {
                if (ci < 0) continue;
                const int n = map.geom().cellPolygon(ci, wx, wy, 12);
                for (int k = 0; k < n; ++k) {
                    minX = std::min(minX, wx[k]);
                    maxX = std::max(maxX, wx[k]);
                    minY = std::min(minY, wy[k]);
                    maxY = std::max(maxY, wy[k]);
                }
            }
            const int sx0 = cam_.toScreenXi(minX), sx1 = cam_.toScreenXi(maxX);
            const int sy0 = cam_.toScreenYi(minY), sy1 = cam_.toScreenYi(maxY);
            block = {std::min(sx0, sx1), std::min(sy0, sy1), std::abs(sx1 - sx0),
                     std::abs(sy1 - sy0)};
        } else {
            // 基建地块外廓（屏幕；y 翻转归一：min/abs）。
            const int sx0 = cam_.toScreenXi(static_cast<double>(c.baseX));
            const int sx1 = cam_.toScreenXi(static_cast<double>(c.baseX + c.w));
            const int sy0 = cam_.toScreenYi(static_cast<double>(c.baseY));
            const int sy1 = cam_.toScreenYi(static_cast<double>(c.baseY + c.h));
            block = {std::min(sx0, sx1), std::min(sy0, sy1), std::abs(sx1 - sx0),
                     std::abs(sy1 - sy0)};
        }
        // 视野剔除：地块矩形与视口不相交 → 整城（细线/图标）跳过。
        if (block.x + block.w < 0 || block.x > cam_.windowWidth() || block.y + block.h < 0 ||
            block.y > cam_.windowHeight())
            continue;

        // 高缩放细线：正式首都用更粗的 render.capital.lineThickness（capitalOutlines），
        // 普通城市/候补用 render.city.lineThickness（outlines）。颜色 = 归属势力加深色。
        if (highZoom) {
            if (tiled) {
                // P12：外廓 = 形状边界线段（邻格不在形状内的边段；cellEdge 与邻接同序）。
                Hull h;
                h.colorIndex = std::clamp(c.ownerId, 0, nColor - 1);
                for (int ci : cells) {
                    if (ci < 0) continue;
                    for (int k = 0; k < map.geom().neighborCount(); ++k) {
                        const int nb = map.geom().neighbor(ci, k);
                        bool inShape = false;
                        for (int other : cells)
                            if (other == nb) {
                                inShape = true;
                                break;
                            }
                        if (inShape) continue;  // 内边
                        double ex0, ey0, ex1, ey1;
                        if (!map.geom().cellEdge(ci, k, ex0, ey0, ex1, ey1)) continue;
                        h.segs.push_back({cam_.toScreenXi(ex0), cam_.toScreenYi(ey0),
                                          cam_.toScreenXi(ex1), cam_.toScreenYi(ey1)});
                    }
                }
                if (status == 1)
                    f.capitalHulls.push_back(std::move(h));
                else
                    f.hulls.push_back(std::move(h));
            } else {
                Outline o{block.x, block.y, block.w, block.h,
                          std::clamp(c.ownerId, 0, nColor - 1)};
                if (status == 1)
                    f.capitalOutlines.push_back(o);
                else
                    f.outlines.push_back(o);
            }
        }

        // 图标尺寸按基建地块框自适应。方：AABB 框（与旧版一致）；六/三/半正/Laves：
        // 普通城市优先用 config 预计算的 iconFitScale（中心 = 基建地块几何中心，不平移）；
        // 无预计算值（旧配置/测试）或首都图标仍回退 CityIconFitter 允许平移的实时拟合（缓存）。
        const std::size_t lvi = static_cast<size_t>(texLevel <= 9 ? texLevel - 1 : 8);
        const double cellPx = cam_.cellPx();

        // 先按普通城市贴图计算尺寸（首都图标面积与该普通贴图面积一致）。
        double normalA = (texLevel >= 10 ? tower10Aspect_ : srcAspect_[lvi]);
        if (normalA <= 0.0) normalA = 1.0;
        double normalFitW = 0.0;
        double normalIconCx = c.centerX(), normalIconCy = c.centerY();
        bool normalDirectFit = false;  // true = iconFitScale 已是最终 fitW，不再乘 iconScale
        if (tiled) {
            bool haveFit = false;
            const auto tIt = rc.city.iconFitScale.find(tilingName(map.tiling()));
            if (tIt != rc.city.iconFitScale.end()) {
                const auto lIt = tIt->second.find(texLevel);
                if (lIt != tIt->second.end() && lIt->second > 0.0) {
                    normalFitW = lIt->second * cellPx;
                    normalIconCx = c.centerX();
                    normalIconCy = c.centerY();
                    haveFit = true;
                    normalDirectFit = true;  // 配置值即最终 fitW
                }
            }
            // 竖直平移（世界单位）：每形状变体 × 每锚基础格类一条（2026-08-26 去重：bases = 该类的
            // 全部基础格，命中 = iconLevel+variant 匹配 且 城市实际 anchorB ∈ bases）。
            const auto oIt = rc.city.iconFitOffsetY.find(tilingName(map.tiling()));
            if (oIt != rc.city.iconFitOffsetY.end()) {
                const int anchorB = (map.geom().baseCount() > 0)
                                        ? (c.baseIndex % map.geom().baseCount())
                                        : 0;
                for (const auto& rec : oIt->second) {
                    if (rec.iconLevel != texLevel || rec.variant != c.shapeVariant) continue;
                    bool inClass = false;
                    for (int b : rec.bases)
                        if (b == anchorB) { inClass = true; break; }
                    if (inClass) {
                        normalIconCy = c.centerY() + rec.value;
                        break;
                    }
                }
            }
            if (!haveFit) {
                // 用真实形状多边形（世界坐标）拟合，而不是 AABB。
                std::vector<FitPoly> polys;
                polys.reserve(cells.size());
                double wx[12], wy[12];
                for (int ci : cells) {
                    if (ci < 0) continue;
                    const int n = map.geom().cellPolygon(ci, wx, wy, 12);
                    if (n < 3) continue;
                    FitPoly poly;
                    poly.reserve(static_cast<size_t>(n));
                    for (int k = 0; k < n; ++k) poly.push_back({wx[k], wy[k]});
                    polys.push_back(std::move(poly));
                }
                const std::string key = std::string(tilingName(map.tiling())) + "|"
                                        + std::to_string(texLevel) + "|"
                                        + std::to_string(static_cast<int>(std::lround(normalA * 1000.0)))
                                        + "|" + std::to_string(c.baseIndex % map.geom().baseCount());
                auto it = fitCache_.find(key);
                if (it == fitCache_.end()) {
                    double s = 0.0, fcx = 0.0, fcy = 0.0;
                    if (CityIconFitter::compute(polys, 1.0, normalA, s, fcx, fcy)) {
                        it = fitCache_.emplace(key, FitEntry{s, fcx - c.centerX(), fcy - c.centerY()})
                                 .first;
                    } else {
                        it = fitCache_.end();
                    }
                }
                if (it != fitCache_.end()) {
                    normalFitW = it->second.scale * cellPx;
                    normalIconCx = c.centerX() + it->second.offX;
                    normalIconCy = c.centerY() + it->second.offY;
                } else {
                    // 兜底：AABB。
                    normalFitW = std::min(static_cast<double>(block.w), static_cast<double>(block.h) / normalA);
                }
            }
        } else {
            const double boxW = static_cast<double>(c.w) * cellPx;
            const double boxH = static_cast<double>(c.h) * cellPx;
            normalFitW = std::min(boxW, boxH / normalA);
        }
        const double normalScaleFactor = normalDirectFit ? 1.0 : rc.city.iconScale[lvi];
        const int normalDstW = std::max(1, static_cast<int>(std::lround(normalFitW * normalScaleFactor)));
        const int normalDstH = std::max(1, static_cast<int>(std::lround(static_cast<double>(normalDstW) * normalA)));

        double iconCx = normalIconCx;
        double iconCy = normalIconCy;
        int dstW = normalDstW;
        int dstH = normalDstH;
        if (isCapital) {
            // 首都贴图：面积 = 普通城市贴图实际面积 S；按首都贴图纵横比反推尺寸；
            // 始终不添加竖直偏移，中心 = 基建地块几何中心。
            double capA = capitalAspect_;
            if (capA <= 0.0) capA = 1.0;
            const double S = static_cast<double>(normalDstW) * static_cast<double>(normalDstH);
            const double capW = std::sqrt(S / capA);
            dstW = std::max(rc.capital.minIconSizePx, static_cast<int>(std::lround(capW)));
            dstH = std::max(1, static_cast<int>(std::lround(static_cast<double>(dstW) * capA)));
            iconCx = c.centerX();
            iconCy = c.centerY();
        }

        const Icon icon{texLevel, cam_.toScreenXi(iconCx), cam_.toScreenYi(iconCy),
                        dstW, dstH, std::clamp(c.ownerId, 0, nColor - 1)};
        if (status == 1)
            f.capitalIcons.push_back(icon);
        else if (status == 2)
            f.designatedIcons.push_back(icon);  // 候补：虚化首都图标（draw 用 designatedAlpha）
        else
            f.icons.push_back(icon);
    }
    return f;
}

void CityRenderer::draw(const Map& map, const Config::Render& rc,
                        const std::vector<int>& capitalStatus) {
    if (!ren_) return;  // 纯几何单测构造（无渲染器）→ 跳过绘制
    const Frame f = compute(map, rc, capitalStatus);
    Renderer r(ren_);

    // 绘制顺序（层级）：普通城市塔图标 → 正式首都图标 → 候补指定虚化图标 → 普通细线 → 首都粗线。
    // 1) 普通城市等级塔图标（城市中心，LOD；尺寸 compute 已按源纵横比算好 dstW/dstH）。
    drawIcons(f.icons, towers_, tower10_, capital_, /*useCapitalTex=*/false, r, 255);
    // 2) 正式首都图标（首都贴图，全不透明）。
    drawIcons(f.capitalIcons, towers_, tower10_, capital_, /*useCapitalTex=*/true, r, 255);
    // 3) 候补指定新都：虚化首都图标（alpha = designatedAlpha）。
    if (rc.capital.designatedAlpha > 0.0)
        drawIcons(f.designatedIcons, towers_, tower10_, capital_, /*useCapitalTex=*/true, r,
                  static_cast<int>(std::lround(255.0 * rc.capital.designatedAlpha)));
    // 4) 细线：普通城市薄线 + 正式首都粗线（颜色 = 该城归属势力色 × lineDarken）。
    drawOutlines(f.outlines, rc.city.lineThickness, rc.city.lineDarken, factionColors_, r);
    drawOutlines(f.capitalOutlines, rc.capital.lineThickness, rc.city.lineDarken, factionColors_, r);
    // P12：六/三角基建地块边界线段（形状外廓；同细线颜色语义）。
    drawHullSegs(f.hulls, rc.city.lineThickness, rc.city.lineDarken, factionColors_, r);
    drawHullSegs(f.capitalHulls, rc.capital.lineThickness, rc.city.lineDarken, factionColors_, r);
}

}  // namespace lw::render
