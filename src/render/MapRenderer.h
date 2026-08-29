// MapRenderer.h — 地图网格渲染（翻新计划 Phase 7，§3.1 render/；P5 山地/城市改版）。
// 逐格：海 = 背景黑（原版零宽 DrawBox 空操作 → 直接跳过，靠清黑兜底）；
// 陆地（含山/城）基色 = 双色调色板 `tileColor(id)`（主:副:白 加权平均，视觉工程改进 ⑫）；
// 再叠画按格面积缩放、线宽保持不变的山纹（近黑：势力色 × render.mountain.colorDarken）。
// P13：城市绘制（基建格 city 贴图 + 等级图标 + 高缩放细线围区）迁出到 render/CityRenderer
// （在 MapRenderer 之后绘制 → 山地基建格山贴图在下、城市贴图在上，顺序不变）。
// 贴图经 TintCache（白底透明 → 目标色，与玩家指示贴图同管线）。纯渲染层，不碰模拟。
#pragma once

#include <array>
#include <vector>

#include <SDL.h>

#include "core/GameDefs.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/TintCache.h"
#include "world/Map.h"

namespace lw::render {

class MapRenderer {
public:
    MapRenderer(SDL_Renderer* ren, const Camera& cam, const Config::Render::Mountain& config)
        : ren_(ren), cam_(cam), mountainConfig_(config) {}

    // 设置山纹颜色（colors 下标即势力 id，含中立 0 共 9 项）；面积变体在首次绘制地图时烘焙。
    // 可重复调用（F5/F6 重烘焙）。
    void bake(const std::vector<std::array<int, 3>>& colors);
    void reloadColors(const std::vector<std::array<int, 3>>& colors) { bake(colors); }

    // 逐格绘制整张地图（经 Camera 缩放/平移，含视野剔除）。tileColors 下标即势力 id
    // （含中立 0 共 9 项）= 双色调色板地块填色（主:副:白 加权，= 渐变中点 t=0.5）。
    // gradeColors（2026-08 双色密铺分档；arch/laves 叠加分档着色）：下标 = 档位 j，
    // 每项下标即势力 id = 该档配色（t = j/(paletteSize-1)）。仅 arch/laves 非空
    // （paletteSize>=2）；方/六/三与单色情形传空 → 用 tileColors 中点色。
    void draw(const Map& map, const std::array<std::array<int, 3>, kFactionTotal>& tileColors,
              const std::vector<std::array<std::array<int, 3>, kFactionTotal>>& gradeColors = {});

private:
    struct MountainScale {
        TintCache textures;
        double scale = 1.0;
    };

    void ensureMountainScales(const Map& map);
    void drawMountain(const Map& map, int index, Renderer& renderer);
    void releaseMountainScales();
    void drawSquare(const Map& map,
                    const std::array<std::array<int, 3>, kFactionTotal>& tileColors);
    void drawTiled(const Map& map,
                   const std::array<std::array<int, 3>, kFactionTotal>& tileColors,
                   const std::vector<std::array<std::array<int, 3>, kFactionTotal>>& gradeColors);
    // P12 决策 3：一圈灰线描出地图边界（任意密铺；实心粗线段）。
    void drawBoundaryOutline(const Map& map);
    SDL_Renderer* ren_;
    const Camera& cam_;
    const Config::Render::Mountain& mountainConfig_;
    std::vector<std::array<int, 3>> mountainColors_;
    std::vector<MountainScale> mountainScales_;
};

}  // namespace lw::render
