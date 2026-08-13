// CityMarkerRenderer.cpp — 玩家城市指示实现（P2 + 2026-08-07 修正；贴图走统一 tint 管线）。
#include "render/CityMarkerRenderer.h"

#include <algorithm>
#include <cmath>

#include "core/GameDefs.h"
#include "core/Simulation.h"

namespace lw::render {

namespace {
constexpr int kTexSize = 128;  // 源贴图（data/ring.png、data/arrow.png）尺寸

// 城市显示亮度：mix_color(势力色, 黑, 0.8) = 势力色 × 0.8（MapRenderer 城市格同款）。
std::array<int, 3> dimCityColor(const std::array<int, 3>& col) {
    std::array<int, 3> out;
    for (int i = 0; i < 3; ++i)
        out[static_cast<size_t>(i)] = static_cast<int>(col[static_cast<size_t>(i)] * 0.8);
    return out;
}
}  // namespace

int CityMarkerRenderer::ringSizePx(int w, int h, double cellPx, double margin) {
    const double outerCells = static_cast<double>(std::max(w, h)) + margin;  // 外圆直径（格）
    return static_cast<int>(std::lround(outerCells * cellPx / kRingOuterFrac));
}

int CityMarkerRenderer::arrowSizePx(int w, int h, double cellPx, double gap) {
    const double tipRadius =
        static_cast<double>(std::max(w, h)) / 2.0 + gap;  // 尖端半径（格）
    return static_cast<int>(std::lround(tipRadius * cellPx / kArrowTipFrac));
}

CityMarkerRenderer::CityMarkerRenderer(SDL_Renderer* ren, const Camera& cam,
                                       const std::vector<std::array<int, 3>>& factionColors)
    : ren_(ren), cam_(cam) {
    bake(factionColors);
}

void CityMarkerRenderer::reloadColors(const std::vector<std::array<int, 3>>& factionColors) {
    bake(factionColors);
}

void CityMarkerRenderer::bake(const std::vector<std::array<int, 3>>& factionColors) {
    // 白底透明源图，整体 tint（grayscaleFirst=false, preserveWhite=0 → 白→目标色）。
    // 用"城市显示亮度"（×0.8）着色，保持 P2 用户反馈调暗后的观感。
    std::vector<std::array<int, 3>> dim;
    dim.reserve(factionColors.size());
    for (const auto& c : factionColors) dim.push_back(dimCityColor(c));
    // 白底透明源图，Multiply 整体 tint（白→目标色），保持 P2 调暗观感。
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
                              const CityTarget* sel) {
    if (playerFactionId < 1 || playerFactionId > kPlayerFactionCount) return;
    if (!hover && !sel) return;
    const int idx = playerFactionId - 1;  // 烘焙的下标 = 势力 id - 1
    const std::uint64_t tick = sim.tickCount();
    const auto& pcfg = sim.config().player;
    const double rotSpeed = pcfg.markerRotateSpeed;
    const double alphaScale = std::clamp(pcfg.markerAlpha, 0.0, 1.0);
    const double cellPx = cam_.cellPx();

    if (hover) {
        // 待选城（按下鼠标会选中）：四箭头指向该城，不旋转。
        // 尺寸随城市基建尺寸自适应：尖端半径 = max(w,h)/2 + markerArrowGap 格
        //（2026-08-07 起间距可调，能指示大小不一的城）。
        const int size = arrowSizePx(hover->w, hover->h, cellPx, pcfg.markerArrowGap);
        const int alpha = static_cast<int>(std::lround(255.0 * alphaScale * 0.9));
        drawTex(arrowTint_.texture(idx), hover->cx, hover->cy, size, 0.0, alpha);
    }
    if (sel) {
        // 产兵城：旋转虚线环（顺时针）。外圆直径 = max(w,h) + markerRingMargin 格，
        // 圈住大小不一的城市（2026-08-07 起可自由缩放）。
        const int size = ringSizePx(sel->w, sel->h, cellPx, pcfg.markerRingMargin);
        const int alpha = static_cast<int>(std::lround(255.0 * alphaScale));
        drawTex(ringTint_.texture(idx), sel->cx, sel->cy, size,
                static_cast<double>(tick) * rotSpeed, alpha);
    }
}

}  // namespace lw::render
