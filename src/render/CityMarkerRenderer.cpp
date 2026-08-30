// CityMarkerRenderer.cpp — 玩家城市指示实现（P2 + 2026-08-07 修正；贴图走统一 tint 管线）。
// 2026-08 细节改进：箭头从"整图按城市大小缩放"改为"拆分四方向子图 + 固定尺寸移动位置"，
// 见 CityMarkerRenderer.h 文件头注。
#include "render/CityMarkerRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/GameDefs.h"
#include "core/Simulation.h"
#include "render/RendererUtil.h"

namespace lw::render {

namespace {
constexpr int kTexSize = 128;  // 源贴图（data/ring.png、data/arrow.png）尺寸

// 四箭头源子图（tools/gen_markers.py 布局：128×128，四箭头尖端距中心 22px、长 20px、
// 半宽 15px，指向中心）。取每支箭头的最小包围盒（含尖端的一侧即指向中心的一侧）：
//   左箭头（指向右，尖端在右缘）→ {22, 49, 20, 30}
//   右箭头（指向左，尖端在左缘）→ {86, 49, 20, 30}
//   上箭头（指向下，尖端在下缘）→ {49, 22, 30, 20}
//   下箭头（指向上，尖端在上缘）→ {49, 86, 30, 20}
constexpr SDL_Rect kArrowLeftSrc{22, 49, 20, 30};
constexpr SDL_Rect kArrowRightSrc{86, 49, 20, 30};
constexpr SDL_Rect kArrowTopSrc{49, 22, 30, 20};
constexpr SDL_Rect kArrowBottomSrc{49, 86, 30, 20};
}  // namespace

int CityMarkerRenderer::ringSizePx(int w, int h, double cellPx, double margin) {
    const double outerWorld = static_cast<double>(std::max(w, h)) + margin;  // 标称外圆直径（U）
    return static_cast<int>(std::lround(outerWorld * cellPx / kRingOuterFrac));
}

double CityMarkerRenderer::spawnArrowDistance(double area, double areaDivisor,
                                               double renderedLength) {
    return std::sqrt(std::max(0.0, area) / std::max(areaDivisor, 1e-12))
           + std::max(0.0, renderedLength) / 2.0;
}

std::array<CityMarkerRenderer::ArrowRect, 4> CityMarkerRenderer::arrowRects(
    const CityTarget& t, double cellPx, double gap) {
    // 固定箭头尺寸（只随 zoom/cellPx；不随城市大小——城市大小由位置吸收）。
    const double lenPx = kArrowLenCells * cellPx;          // 箭头长（U 转逻辑像素）
    const double widPx = lenPx * kArrowWidthOverLen;       // 箭头宽
    const double hw = static_cast<double>(t.w) * cellPx / 2.0;  // 城市框半宽（屏幕 px）
    const double hh = static_cast<double>(t.h) * cellPx / 2.0;  // 城市框半高
    const double gapPx = gap * cellPx;                     // 尖端与框缘间距
    // 尖端位置：左/右箭头在框左/右缘中点，上/下箭头在框上/下缘中点，均外移 gapPx。
    const double tipL = t.cx - hw - gapPx;
    const double tipR = t.cx + hw + gapPx;
    const double tipT = t.cy - hh - gapPx;
    const double tipB = t.cy + hh + gapPx;
    // 绘制中心 = 尖端沿指向方向回退 lenPx/2（水平箭头尖端在左/右缘、垂直在上下缘），
    // 使尖端恰好落在 (框缘 + gapPx) 处。
    return {{
        {tipL - lenPx / 2.0, t.cy, static_cast<int>(std::lround(lenPx)),
         static_cast<int>(std::lround(widPx))},  // 左（指向右）
        {tipR + lenPx / 2.0, t.cy, static_cast<int>(std::lround(lenPx)),
         static_cast<int>(std::lround(widPx))},  // 右（指向左）
        {t.cx, tipT - lenPx / 2.0, static_cast<int>(std::lround(widPx)),
         static_cast<int>(std::lround(lenPx))},  // 上（指向下）
        {t.cx, tipB + lenPx / 2.0, static_cast<int>(std::lround(widPx)),
         static_cast<int>(std::lround(lenPx))},  // 下（指向上）
    }};
}

CityMarkerRenderer::CityMarkerRenderer(SDL_Renderer* ren, const Camera& cam,
                                       const std::vector<std::array<int, 3>>& factionColors)
    : ren_(ren), cam_(cam) {
    bake(factionColors);
}

void CityMarkerRenderer::reloadColors(const std::vector<std::array<int, 3>>& factionColors) {
    bake(factionColors);
}

void CityMarkerRenderer::reloadSpawnArrowColors(
    const std::vector<std::array<int, 3>>& factionColors) {
    // arrow2.png 已按主/副/黑权重在 Config 中合成，保留该颜色，不再额外调暗。
    spawnArrowTint_.load(ren_, "data/arrow2.png", factionColors, TintMode::Multiply,
                         /*grayscaleFirst=*/false, /*preserveWhite=*/0);
}

void CityMarkerRenderer::bake(const std::vector<std::array<int, 3>>& factionColors) {
    // 白底透明源图，整体 tint（grayscaleFirst=false, preserveWhite=0 → 白→目标色）。
    // 用"城市显示亮度"（×0.8）着色，保持 P2 用户反馈调暗后的观感。
    std::vector<std::array<int, 3>> dim;
    dim.reserve(factionColors.size());
    for (const auto& c : factionColors) dim.push_back(scaledColor(c, 0.8));  // 城市显示亮度 ×0.8
    ringTint_.load(ren_, "data/ring.png", dim, TintMode::Multiply, /*grayscaleFirst=*/false,
                   /*preserveWhite=*/0);
    arrowTint_.load(ren_, "data/arrow.png", dim, TintMode::Multiply, /*grayscaleFirst=*/false,
                    /*preserveWhite=*/0);
}

void CityMarkerRenderer::drawTex(SDL_Texture* tex, double worldX, double worldY, int sizePx,
                                 double angleRad, int alpha) const {
    if (!tex) return;
    const int cx = cam_.toScreenXi(worldX);
    const int cy = cam_.toScreenYi(worldY);
    const int sz = std::max(8, sizePx);
    Renderer r(ren_);
    r.drawSpriteRotated(tex, {0, 0, kTexSize, kTexSize}, cx - sz / 2, cy - sz / 2, sz, sz,
                        angleRad * 180.0 / kPi, sz / 2, sz / 2, alpha);
}

void CityMarkerRenderer::draw(const Simulation& sim, int playerFactionId, const CityTarget* hover,
                              const CityTarget* sel,
                              const std::vector<SpawnDirection>& spawnDirections) {
    if ((hover || sel) && (playerFactionId < 1 || playerFactionId > kPlayerFactionCount)) return;
    if (!hover && !sel && spawnDirections.empty()) return;
    const int idx = playerFactionId - 1;  // 烘焙的下标 = 势力 id - 1
    const std::uint64_t tick = sim.tickCount();
    const auto& pcfg = sim.config().render.player;
    const double rotSpeed = pcfg.markerRotateSpeed;
    const double alphaScale = std::clamp(pcfg.markerAlpha, 0.0, 1.0);
    const double cellPx = cam_.cellPx();

    if (hover) {
        // 待选城（按下鼠标会选中）：四箭头指向该城，不旋转。
        // 2026-08 细节改进：拆分四方向子图、固定尺寸、位置随城市框移动（大城箭头外移）。
        // 尺寸只随 zoom（cellPx），与其余精灵一致；尖端与框缘间距 = markerArrowGap U。
        // **注意**：arrowRects 在屏幕坐标空间计算——先把城市中心从世界坐标转换到屏幕
        // （2026-08 修复：漏掉转换曾把箭头画到"世界坐标当像素"的错位位置 → 完全不可见）。
        const CityTarget screenTarget{cam_.toScreenX(hover->cx), cam_.toScreenY(hover->cy),
                                      hover->w, hover->h};
        const auto rects = arrowRects(screenTarget, cellPx, pcfg.markerArrowGap);
        const int alpha = static_cast<int>(std::lround(255.0 * alphaScale * 0.9));
        SDL_Texture* tex = arrowTint_.texture(idx);
        if (tex && ren_) {
            Renderer r(ren_);
            r.drawSpriteCenteredRect(tex, kArrowLeftSrc, rects[0].cx, rects[0].cy, rects[0].dstW,
                                     rects[0].dstH, alpha);
            r.drawSpriteCenteredRect(tex, kArrowRightSrc, rects[1].cx, rects[1].cy, rects[1].dstW,
                                     rects[1].dstH, alpha);
            r.drawSpriteCenteredRect(tex, kArrowTopSrc, rects[2].cx, rects[2].cy, rects[2].dstW,
                                     rects[2].dstH, alpha);
            r.drawSpriteCenteredRect(tex, kArrowBottomSrc, rects[3].cx, rects[3].cy, rects[3].dstW,
                                     rects[3].dstH, alpha);
        }
    }
    if (sel) {
        // 产兵城：旋转虚线环（顺时针）。标称外圆直径 = max(w,h) + markerRingMargin U，
        // 圈住大小不一的城市（2026-08-07 起可自由缩放）。
        const int size = ringSizePx(sel->w, sel->h, cellPx, pcfg.markerRingMargin);
        const int alpha = static_cast<int>(std::lround(255.0 * alphaScale));
        drawTex(ringTint_.texture(idx), sel->cx, sel->cy, size,
                static_cast<double>(tick) * rotSpeed, alpha);
    }
    drawSpawnDirections(spawnDirections, sim.config().render.city.spawnArrow.areaDivisor);
}

void CityMarkerRenderer::drawSpawnDirections(const std::vector<SpawnDirection>& arrows,
                                             double areaDivisor) const {
    if (!ren_ || arrows.empty() || spawnArrowTint_.count() == 0) return;
    const double sizeCells = 0.9;
    const int size = std::max(8, static_cast<int>(std::lround(sizeCells * cam_.cellPx())));
    const SDL_Rect src{0, 0, spawnArrowTint_.width(), spawnArrowTint_.height()};
    Renderer r(ren_);
    for (const auto& arrow : arrows) {
        const int colorIndex = arrow.factionId - 1;
        if (colorIndex < 0 || colorIndex >= spawnArrowTint_.count()) continue;
        SDL_Texture* tex = spawnArrowTint_.texture(colorIndex);
        if (!tex) continue;
        const double renderedLengthWorld = static_cast<double>(size) / cam_.cellPx();
        const double offset = spawnArrowDistance(arrow.target.area, areaDivisor,
                                                 renderedLengthWorld);
        const double worldX = arrow.target.cx + offset * std::cos(arrow.angleRad);
        const double worldY = arrow.target.cy + offset * std::sin(arrow.angleRad);
        const int arrowCx = cam_.toScreenXi(worldX);
        const int arrowCy = cam_.toScreenYi(worldY);
        // arrow2.png 默认尖端向屏幕上方；世界角 0 向右，屏幕 y 轴反向，故旋转角为 90°-worldAngle。
        const double degrees = 90.0 - arrow.angleRad * 180.0 / kPi;
        r.drawSpriteRotated(tex, src, arrowCx - size / 2, arrowCy - size / 2, size, size,
                            degrees, size / 2, size / 2, 235);
    }
}

}  // namespace lw::render
