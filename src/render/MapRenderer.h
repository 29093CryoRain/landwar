// MapRenderer.h — 地图网格渲染（翻新计划 Phase 7，§3.1 render/；P5 山地/城市改版）。
// 逐格：海 = 背景黑（原版零宽 DrawBox 空操作 → 直接跳过，靠清黑兜底）；
// 陆地（含山/城）基色 = mix_color(势力色, 白, 0.6)；再叠画线条贴图——
//   山格 → data/mountain.png（近黑：mix_color(势力色,黑,0.08)）。
// P13：城市绘制（基建格 city 贴图 + 等级图标 + 高缩放细线围区）迁出到 render/CityRenderer
// （在 MapRenderer 之后绘制 → 山地基建格山贴图在下、城市贴图在上，顺序不变）。
// 贴图经 TintCache（白底透明 → 目标色，与玩家指示贴图同管线）。纯渲染层，不碰模拟。
#pragma once

#include <array>
#include <vector>

#include <SDL.h>

#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/TintCache.h"
#include "world/Faction.h"
#include "world/Map.h"

namespace lw::render {

class MapRenderer {
public:
    MapRenderer(SDL_Renderer* ren, const Camera& cam) : ren_(ren), cam_(cam) {}

    // 烘焙山线条贴图（factionColors 下标即势力 id，含中立 0 共 9 项）。可重复调用（F5 重烘焙）。
    void bake(const std::vector<std::array<int, 3>>& factionColors);
    void reloadColors(const std::vector<std::array<int, 3>>& factionColors) { bake(factionColors); }

    // 逐格绘制整张地图（经 Camera 缩放/平移，含视野剔除）。factions 下标即势力 id（含中立 0）。
    void draw(const Map& map, const std::vector<Faction>& factions);

private:
    SDL_Renderer* ren_;
    const Camera& cam_;
    TintCache mountain_;  // 山线条贴图（近黑）
};

}  // namespace lw::render
