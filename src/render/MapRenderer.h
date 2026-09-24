// MapRenderer.h — 地图网格渲染（翻新计划 Phase 7，§3.1 render/；P5 山地/城市改版）。
// 逐格：海 = 背景黑（原版零宽 DrawBox 空操作 → 直接跳过，靠清黑兜底）；
// 陆地（含山/城）基色 = 双色调色板 `tileColor(id)`（主:副:白 加权平均，视觉工程改进 ⑫）；
// 再叠画按格面积缩放、线宽保持不变的山纹（近黑：势力色 × render.mountain.colorDarken）。
// P13：城市绘制（基建格 city 贴图 + 等级图标 + 高缩放细线围区）迁出到 render/CityRenderer
// （在 MapRenderer 之后绘制 → 山地基建格山贴图在下、城市贴图在上，顺序不变）。
// 贴图经 TintCache（白底透明 → 目标色，与玩家指示贴图同管线）。纯渲染层，不碰模拟。
#pragma once

#include <array>
#include <cstdint>
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
    void draw(const Map& map, const std::vector<std::array<int, 3>>& tileColors,
              const std::vector<std::vector<std::array<int, 3>>>& gradeColors = {});
    // 旧测试/调用方兼容入口；新代码应使用运行时长度的 vector 版本。
    void draw(const Map& map, const std::array<std::array<int, 3>, kFactionTotal>& tileColors);
    // Developer-tool grid layer. The regular game draw path keeps only the
    // outer boundary; editor callers can omit this at low zoom.
    void drawGrid(const Map& map, const SDL_Color& color = {235, 235, 235, 255});

private:
    struct MountainScale {
        TintCache textures;
        double scale = 1.0;
    };

    // Per-cell world-space polygon cache for drawTiled.  Rebuilt only when
    // the map geometry (tiling type / cols / rows) changes; camera changes
    // only re-transform to screen space, avoiding repeated cellPolygon calls.
    struct CellPolyCache {
        TilingType tiling = TilingType::Square;
        int cols = 0;
        int rows = 0;
        int cellCount = 0;
        std::vector<std::uint8_t> vertCount;   // per cell: polygon vertex count
        std::vector<std::size_t> cellOffset;   // per cell: flat index into wx/wy arrays
        std::vector<double> wx;                // flattened world x coords
        std::vector<double> wy;                // flattened world y coords
    };

    void ensureMountainScales(const Map& map);
    void drawMountain(const Map& map, int index, Renderer& renderer);
    void releaseMountainScales();
    void drawSquare(const Map& map, const std::vector<std::array<int, 3>>& tileColors);
    void drawTiled(const Map& map, const std::vector<std::array<int, 3>>& tileColors,
                   const std::vector<std::vector<std::array<int, 3>>>& gradeColors);
    void ensureTiledVertices(const Map& map);
    // P12 决策 3：一圈灰线描出地图边界（任意密铺；实心粗线段）。
    void drawBoundaryOutline(const Map& map);
    SDL_Renderer* ren_;
    const Camera& cam_;
    const Config::Render::Mountain& mountainConfig_;
    std::vector<std::array<int, 3>> mountainColors_;
    std::vector<MountainScale> mountainScales_;
    CellPolyCache polyCache_;
    // 逐帧复用缓冲（保留容量，避免每帧重建批次容器；2026-09 性能修复）。
    std::vector<std::vector<SDL_Rect>> squareBatches_;  // drawSquare：按势力分组的格矩形
    std::vector<SDL_Color> groupColors_;                // drawSquare/drawTiled：势力→颜色
    std::vector<SDL_Vertex> vertexScratch_;             // drawGrid/drawTiled：顶点批次
};

}  // namespace lw::render
