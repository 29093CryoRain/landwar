// CityMarkerRenderer.h — 玩家城市指示（开发计划 P2 + 2026-08-07 修正；贴图走统一 tint 管线）。
// 两类指示（都按"城市显示亮度" = mix_color(势力色, 黑, 0.8) 着色，用户反馈太亮→调暗）：
//   悬停/待选（按下鼠标会选中的城市）：四箭头指向该城（data/arrow.png 拆分，不旋转）；
//   产兵城（已选）：旋转虚线环（data/ring.png）。
// 贴图由 tools/gen_markers.py 生成的白底透明美术图，TintCache 按势力色烘焙多版本存内存。
// 悬停目标每帧由 Application::updatePlayerIntent 计算（范围内有可选城即命中，无需鼠标移动）。
// 旋转角来自 sim.tickCount()（世界时钟），**绝不触碰 sim RNG**。
//
// 2026-08-07 指示尺寸自适应：环/箭头按目标城市基建地块尺寸（w,h）——环的公式
//   （外圆直径 = max(w,h)+markerRingMargin 格 / kRingOuterFrac）保持不变。
// 2026-08 细节改进（箭头拆分）：原实现把整张四箭头图按城市大小缩放（大城箭头跟着变大，
// 小城缩向中心，观感生硬）。现改为**拆分 arrow.png 为 4 个方向子图，不随城市大小缩放**：
// 每个箭头以固定尺寸（长度 = kArrowLenCells 格 × cellPx，缩放只随 zoom，与其余精灵一致）
// 绘制，位置随城市基建框移动——左/右箭头尖端对齐框左/右缘中点、上/下箭头尖端对齐
// 框上/下缘中点，尖端与框缘间距 = markerArrowGap 格 × cellPx。大城箭头自然外移、小城内收。
// 纯几何（arrowRects/ringSizePx）为静态，可离屏单测（无 SDL 渲染器）。
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

    // 单箭头屏幕绘制矩形（纯几何；src 方向由 draw 按下标选择）。
    struct ArrowRect {
        double cx = 0.0, cy = 0.0;  // 绘制中心（屏幕 px；已按"尖端对齐"回退 lenPx/2）
        int dstW = 0, dstH = 0;     // 绘制尺寸（px）
    };

    // 源贴图比例常数（tools/gen_markers.py）：
    static constexpr double kRingOuterFrac = 104.0 / 128.0;  // 环外圆直径占源贴图 = 52×2/128
    // 箭头基准尺寸（2026-08 拆分后）：长度（指向方向）按格计，缩放只随 zoom（cellPx）。
    static constexpr double kArrowLenCells = 1.4;   // 箭头长（格；zoom1 时 ≈21px）
    static constexpr double kArrowWidthOverLen = 1.5;  // 源图宽:长 = 30:20

    // 环纹理边长（px）：外圆直径 = max(w,h)+margin 格（除以 kRingOuterFrac 换算回贴图边长）。
    static int ringSizePx(int w, int h, double cellPx, double margin);
    // 四箭头屏幕布局（顺序：左/右/上/下）：固定尺寸 + 随城市框移动（见文件头注）。
    static std::array<ArrowRect, 4> arrowRects(const CityTarget& t, double cellPx, double gap);

    // factionColors：势力 1..8 的颜色（烘焙"城市显示亮度"版本，即 ×0.8）。
    CityMarkerRenderer(SDL_Renderer* ren, const Camera& cam,
                       const std::vector<std::array<int, 3>>& factionColors);
    ~CityMarkerRenderer() = default;
    CityMarkerRenderer(const CityMarkerRenderer&) = delete;
    CityMarkerRenderer& operator=(const CityMarkerRenderer&) = delete;

    // 绘制指示。playerFactionId 为玩家势力；hover/sel 为待选城（四箭头）与产兵城（旋转环），
    // 含中心 + 基建尺寸（nullptr = 无）。
    void draw(const Simulation& sim, int playerFactionId, const CityTarget* hover,
              const CityTarget* sel);

    // F5 重载配置：势力色可能变 → 重新烘焙。
    void reloadColors(const std::vector<std::array<int, 3>>& factionColors);

private:
    // 旋转纹理绘制（产兵城虚线环用；箭头不走此路径——四方向子图见 draw）。
    void drawTex(SDL_Texture* tex, double worldX, double worldY, int sizePx, double angleRad,
                 int alpha) const;
    // 用 dimCityColor(势力色) 烘焙。
    void bake(const std::vector<std::array<int, 3>>& factionColors);

    SDL_Renderer* ren_;
    const Camera& cam_;
    TintCache ringTint_;   // 128×128 虚线环（按势力色烘焙）
    TintCache arrowTint_;  // 128×128 四箭头（按势力色烘焙；绘制时取四方向子图）
};

}  // namespace lw::render
}  // namespace lw
