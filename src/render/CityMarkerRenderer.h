// CityMarkerRenderer.h — 玩家城市指示（开发计划 P2 + 2026-08-07 修正；贴图走统一 tint 管线）。
// 两类指示（都按"城市显示亮度" = mix_color(势力色, 黑, 0.8) 着色，用户反馈太亮→调暗）：
//   悬停/待选（按下鼠标会选中的城市）：四箭头指向该城（data/arrow.png，不旋转，恒指向中心）；
//   产兵城（已选）：旋转虚线环（data/ring.png）。
// 贴图由 tools/gen_markers.py 生成的白底透明美术图，TintCache 按势力色烘焙多版本存内存。
// 悬停目标每帧由 Application::updatePlayerIntent 计算（范围内有可选城即命中，无需鼠标移动）。
// 旋转角来自 sim.tickCount()（世界时钟），**绝不触碰 sim RNG**。
//
// 2026-08-07 指示尺寸自适应：
//   环/箭头不再用固定格数（4.2/4.0），而按目标城市基建地块尺寸（w,h）缩放——
//   draw() 接受 CityTarget{中心, w, h}（而非裸坐标），尺寸公式：
//     环纹理边长 = (max(w,h)+markerRingMargin) 格 / kRingOuterFrac  （外圆直径圈住整城，
//       外径须 ≥ 对角 √(w²+h²) 才圈住全部基建格，3×3 → margin≥1.24，默认 1.5）；
//     箭头纹理边长 = (max(w,h)/2+markerArrowGap) 格 / kArrowTipFrac  （尖端半径 = 最大
//       半维 + 间距，四箭头停在城基建地块边缘外、间距可调，能指示大小不一的城）。
//   中心取 CityTarget.cx/cy（城市几何中心，非锚点格）——选取/指示均落在产兵城中央。
// 纯尺寸函数（ringSizePx/arrowSizePx）为静态，可离屏单测（无 SDL 渲染器）。
#pragma once

#include <SDL.h>

#include <array>
#include <vector>

#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/TintCache.h"

namespace lw {
class Simulation;  // 前向声明

namespace render {

class CityMarkerRenderer {
public:
    // 目标城市（中心 + 基建地块尺寸）。由 Application 每帧从 Map.cities() 解析（id → target）。
    struct CityTarget {
        double cx = 0.0, cy = 0.0;  // 城市几何中心（世界坐标；baseX+w/2, baseY+h/2）
        int w = 1, h = 1;           // 基建地块尺寸（格）
    };

    // 源贴图比例常数（tools/gen_markers.py）：
    static constexpr double kRingOuterFrac = 104.0 / 128.0;  // 环外圆直径占源贴图 = 52×2/128
    static constexpr double kArrowTipFrac = 22.0 / 128.0;    // 箭头尖端到中心占源贴图 = 22/128

    // 环纹理边长（px）：外圆直径 = max(w,h)+margin 格（除以 kRingOuterFrac 换算回贴图边长）。
    static int ringSizePx(int w, int h, double cellPx, double margin);
    // 箭头纹理边长（px）：尖端半径 = max(w,h)/2 + gap 格（除以 kArrowTipFrac 换算回贴图边长）。
    static int arrowSizePx(int w, int h, double cellPx, double gap);

    // factionColors：势力 1..8 的颜色（烘焙"城市显示亮度"版本，即 ×0.8）。
    CityMarkerRenderer(SDL_Renderer* ren, const Camera& cam,
                       const std::vector<std::array<int, 3>>& factionColors);
    ~CityMarkerRenderer() = default;
    CityMarkerRenderer(const CityMarkerRenderer&) = delete;
    CityMarkerRenderer& operator=(const CityMarkerRenderer&) = delete;

    // 绘制指示。playerFactionId 为玩家势力；hover/sel 为待选城（四箭头）与产兵城（旋转环），
    // 含中心 + 基建尺寸（2026-08-07 起按城市尺寸缩放指示；nullptr = 无）。
    void draw(const Simulation& sim, int playerFactionId, const CityTarget* hover,
              const CityTarget* sel);

    // F5 重载配置：势力色可能变 → 重新烘焙。
    void reloadColors(const std::vector<std::array<int, 3>>& factionColors);

private:
    void drawTex(SDL_Texture* tex, double worldX, double worldY, int sizePx, double angleRad,
                 int alpha) const;
    // 用 dimCityColor(势力色) 烘焙。
    void bake(const std::vector<std::array<int, 3>>& factionColors);

    SDL_Renderer* ren_;
    const Camera& cam_;
    TintCache ringTint_;   // 128×128 虚线环（按势力色烘焙）
    TintCache arrowTint_;  // 128×128 四箭头（按势力色烘焙）
};

}  // namespace lw::render
}  // namespace lw
