// CityRenderer.cpp — P13 城市渲染实现（render/CityRenderer）。
// 2026-08-07 用户反馈：去除 data/city.png 基建格贴图（城市只由等级塔图标 + 高缩放细线表示）；
// 细线颜色 = 势力色加深（render.city.lineDarken），与城市图标同势力色处理。
#include "render/CityRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/GameDefs.h"

namespace lw::render {

namespace {
constexpr int kTerrainTexSize = 128;  // 塔贴图 LOD 档位基准（源图尺寸可小于此 → 档位被忽略）
constexpr int kTowerCount = 9;        // 等级塔贴图数（等级 1..9）
constexpr unsigned kBlack = 0x000000u;  // mix 用黑（细线加深 = 势力色 × lineDarken）

// 等级塔源图路径（data/tower/tower<N>.png；文件名编号即等级）。
std::string towerPath(int level) {
    return "data/tower/tower" + std::to_string(level) + ".png";
}

// 首都图标源图路径（P15：data/tower/capital.png，白色前景透明背景，与其他城市贴图同类）。
std::string capitalPath() { return "data/tower/capital.png"; }

// LOD 档位选择：选预烘焙尺寸与目标屏上尺寸最近的档（同距优先大档 → 尽量降采样而非放大）。
// 与 MapRenderer 同款（CityRenderer 独立维护，避免跨文件匿名命名空间依赖）。
int pickSizeIndex(const TintCache& cache, int target) {
    int best = 0, bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < cache.sizeCount(); ++i) {
        const int d = std::abs(cache.sizePixel(i) - target);
        if (d < bestDist) {  // 严格小于 → 平局保留先出现的（更大档位）
            bestDist = d;
            best = i;
        }
    }
    return best;
}

// 单通道按比例缩放（col × rate；与 MapRenderer::scaled 同款，CityRenderer 独立维护）。
std::array<int, 3> scaled(const std::array<int, 3>& col, double rate) {
    std::array<int, 3> out;
    for (int i = 0; i < 3; ++i)
        out[static_cast<size_t>(i)] =
            static_cast<int>(col[static_cast<size_t>(i)] * rate + 0.5);
    return out;
}

// 绘制一批图标（等级塔：towers_[level-1]；首都/候补：capital_）。alpha 施加于纹理（虚化候补）。
// 共用 pickSizeIndex LOD 选档 + 按该档实际纹理尺寸采样（塔图非方形，2026-08-07 偏移修复）。
void drawIcons(const std::vector<CityRenderer::Icon>& icons,
               const std::array<TintCache, 9>& towers, const TintCache& capital,
               bool useCapitalTex, Renderer& r, int alpha) {
    for (const auto& ic : icons) {
        const TintCache& cache = useCapitalTex ? capital : towers[static_cast<size_t>(ic.level - 1)];
        if (cache.count() == 0) continue;
        const int ci = std::clamp(ic.colorIndex, 0, cache.count() - 1);
        const int si = pickSizeIndex(cache, ic.dstW);
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
}  // namespace

void CityRenderer::bake(const std::vector<std::array<int, 3>>& factionColors,
                        double iconDarken) {
    factionColors_ = factionColors;  // 留存原势力色（细线加深查表用）
    // 等级塔 + 首都图标：白底透明线条源图 → 目标色（Multiply，与玩家指示/山同管线）。
    // 图标色加深（render.city.iconDarken）：势力色 × 系数，避免亮色图标在亮陆地上对比弱
    //（2026-08-07 反馈大城看不清 → 调暗）。细线仍用原色 × lineDarken（draw 里单独混黑）。
    std::vector<std::array<int, 3>> iconCols;
    iconCols.reserve(factionColors.size());
    for (const auto& c : factionColors) iconCols.push_back(scaled(c, iconDarken));
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
    // P15：首都图标贴图（data/tower/capital.png，同塔管线）。
    capital_.load(ren_, capitalPath(), iconCols, TintMode::Multiply,
                  /*grayscaleFirst=*/false, /*preserveWhite=*/0, lodSizes);
    if (capital_.width() > 0)
        capitalAspect_ = static_cast<double>(capital_.height()) / static_cast<double>(capital_.width());
}

void CityRenderer::setTowerSourceSize(int level, int srcW, int srcH) {
    if (level < 1 || level > kTowerCount || srcW <= 0 || srcH <= 0) return;
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
        // 等级塔/首都图标缩放表按等级索引（1..9）；等级不在 1..9（防御）→ 跳过该城。
        if (c.level < 1 || c.level > kTowerCount) continue;
        const int status = statusOf(c);
        const bool isCapital = (status == 1 || status == 2);  // 正式首都 / 候补（都用首都图标）

        // 基建地块外廓（屏幕；y 翻转归一：min/abs）。
        const int sx0 = cam_.toScreenXi(static_cast<double>(c.baseX));
        const int sx1 = cam_.toScreenXi(static_cast<double>(c.baseX + c.w));
        const int sy0 = cam_.toScreenYi(static_cast<double>(c.baseY));
        const int sy1 = cam_.toScreenYi(static_cast<double>(c.baseY + c.h));
        const SDL_Rect block = {std::min(sx0, sx1), std::min(sy0, sy1),
                                std::abs(sx1 - sx0), std::abs(sy1 - sy0)};
        // 视野剔除：地块矩形与视口不相交 → 整城（细线/图标）跳过。
        if (block.x + block.w < 0 || block.x > cam_.windowWidth() || block.y + block.h < 0 ||
            block.y > cam_.windowHeight())
            continue;

        // 高缩放细线：正式首都用更粗的 render.capital.lineThickness（capitalOutlines），
        // 普通城市/候补用 render.city.lineThickness（outlines）。颜色 = 归属势力加深色。
        if (highZoom) {
            Outline o{block.x, block.y, block.w, block.h, std::clamp(c.ownerId, 0, nColor - 1)};
            if (status == 1)
                f.capitalOutlines.push_back(o);
            else
                f.outlines.push_back(o);
        }

        // 图标尺寸按基建地块框自适应（2026-08-07 反馈；与等级塔同套路）：
        //   框宽 boxW = c.w×cellPx、框高 boxH = c.h×cellPx；A = 源纵横比（高/宽）；
        //   保留纵横比下能放进框的最大宽 fitW = min(boxW, boxH/A)；
        //   图标宽 = round(fitW×iconScale[level-1])，图标高 = round(图标宽×A)。
        //   正式首都/候补用 render.capital.iconScale + 首都源纵横比，普通城市用
        //   render.city.iconScale + 等级塔源纵横比。
        // **渲染改版（用户定夺 2026-08-08）**：普通城市**随缩放继续缩小**（无下限，仅保 ≥1px）；
        //   正式首都/候补**不随缩放继续缩小**（保底 render.capital.minIconSizePx，低缩放可见、
        //   比普通城市更显眼——用户定夺：缩到最小时首都渲染得更大）。
        const std::size_t lvi = static_cast<size_t>(c.level - 1);
        const double cellPx = cam_.cellPx();
        const double boxW = static_cast<double>(c.w) * cellPx;
        const double boxH = static_cast<double>(c.h) * cellPx;
        double A = isCapital ? capitalAspect_ : srcAspect_[lvi];
        if (A <= 0.0) A = 1.0;  // 未烘焙/未知 → 按方形
        const double scale = isCapital ? rc.capital.iconScale[lvi] : rc.city.iconScale[lvi];
        const double fitW = std::min(boxW, boxH / A);
        const int dstW = isCapital
                             ? std::max(rc.capital.minIconSizePx,
                                        static_cast<int>(std::lround(fitW * scale)))
                             : std::max(1, static_cast<int>(std::lround(fitW * scale)));
        const int dstH = std::max(1, static_cast<int>(std::lround(static_cast<double>(dstW) * A)));
        // 图标中心 = 基建地块几何中心（2026-08-07 用户要求；移除 offsetY 错开）。
        const Icon icon{c.level, cam_.toScreenXi(c.centerX()), cam_.toScreenYi(c.centerY()),
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
    drawIcons(f.icons, towers_, capital_, /*useCapitalTex=*/false, r, 255);
    // 2) 正式首都图标（首都贴图，全不透明）。
    drawIcons(f.capitalIcons, towers_, capital_, /*useCapitalTex=*/true, r, 255);
    // 3) 候补指定新都：虚化首都图标（alpha = designatedAlpha）。
    if (rc.capital.designatedAlpha > 0.0)
        drawIcons(f.designatedIcons, towers_, capital_, /*useCapitalTex=*/true, r,
                  static_cast<int>(std::lround(255.0 * rc.capital.designatedAlpha)));
    // 4) 细线：普通城市薄线 + 正式首都粗线（颜色 = 该城归属势力色 × lineDarken）。
    drawOutlines(f.outlines, rc.city.lineThickness, rc.city.lineDarken, factionColors_, r);
    drawOutlines(f.capitalOutlines, rc.capital.lineThickness, rc.city.lineDarken, factionColors_, r);
}

}  // namespace lw::render
