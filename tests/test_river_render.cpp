// test_river_render.cpp — 河流渲染的 SDL 软件渲染回归（《河流系统开发文档》§7.2/§7.3）。
// 断言：有河 → 河边中点/河顶点出现黑线黑点；无河 → 同点仍是地块色；LOD 命中整层不画；
// 颜色配置生效；**线宽随缩放同比变化且有最小宽度**（世界量 widthU → 屏幕 px）；同一线宽
// 在两条不同朝向的河边上一致（无"粗细不均"）；缩略预览也画出河且遵守同一条线宽规则。
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "core/Config.h"
#include "render/Camera.h"
#include "render/MapPreview.h"
#include "render/MapRenderer.h"
#include "world/Map.h"
#include "world/MapDefinition.h"

namespace {

constexpr int kW = 16;
constexpr int kH = 16;
constexpr int kBlockSize = 10;  // 逻辑像素 / 格

lw::Map makeMap(bool withRivers, const lw::Config& cfg) {
    lw::MapDefinition definition;
    definition.cols = kW;
    definition.rows = kH;
    definition.tiling = lw::TilingType::Square;
    definition.terrain.assign(static_cast<std::size_t>(kW) * kH, lw::MapTerrain::Land);
    if (withRivers) definition.rivers = {{0, 3}, {0, 0}};
    lw::Map map;
    map.configureCanonical(lw::TilingType::Square, kW, kH);
    map.setTerrain(cfg.terrain);
    map.setCityConfig(cfg.city);
    EXPECT_TRUE(map.loadFromDefinition(definition));
    return map;
}

Uint32 pixelAt(const SDL_Surface* surface, int x, int y) {
    const Uint32* px = static_cast<const Uint32*>(surface->pixels);
    return px[static_cast<std::size_t>(y) * (surface->pitch / 4) + static_cast<std::size_t>(x)];
}

bool isDark(Uint32 pixel, const SDL_PixelFormat* format) {
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_GetRGBA(pixel, format, &r, &g, &b, &a);
    return r < 60 && g < 60 && b < 60;
}

bool isWhite(Uint32 pixel, const SDL_PixelFormat* format) {
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_GetRGBA(pixel, format, &r, &g, &b, &a);
    return r > 200 && g > 200 && b > 200;
}

// 竖直线 x 上、y∈[y0,y1] 内的近黑像素数（测线宽用）。
int darkInColumn(const SDL_Surface* surface, int x, int y0, int y1) {
    int dark = 0;
    for (int y = y0; y <= y1; ++y)
        if (isDark(pixelAt(surface, x, y), surface->format)) ++dark;
    return dark;
}

// 水平线 y 上、x∈[x0,x1] 内的近黑像素数。
int darkInRow(const SDL_Surface* surface, int y, int x0, int x1) {
    int dark = 0;
    for (int x = x0; x <= x1; ++x)
        if (isDark(pixelAt(surface, x, y), surface->format)) ++dark;
    return dark;
}

// 预览表面里的近黑像素数（预览地块配色为灰/褐/黄，黑只可能来自河）。
int countDarkPixels(const SDL_Surface* surface) {
    const int w = surface->w, h = surface->h;
    int dark = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            SDL_GetRGBA(pixelAt(surface, x, y), surface->format, &r, &g, &b, &a);
            if (r < 60 && g < 60 && b < 60) ++dark;
        }
    return dark;
}

TEST(RiverRender, DrawsRiversWithColorWidthAndLod) {
    ASSERT_EQ(SDL_Init(0), 0) << SDL_GetError();
    SDL_Surface* target = SDL_CreateRGBSurfaceWithFormat(0, 800, 600, 32, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(target, nullptr) << SDL_GetError();
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    ASSERT_NE(renderer, nullptr) << SDL_GetError();

    const lw::Config cfg = lw::Config::loadFromJson("{}");
    lw::render::Camera camera;
    camera.configure(
        lw::math::ScreenTransform{static_cast<double>(kBlockSize), 0, static_cast<double>(kH)},
        800, 600, kW, kH);
    std::vector<std::array<int, 3>> colors(9, {255, 255, 255});
    std::array<std::array<int, 3>, lw::kFactionTotal> tileColors{};
    for (auto& color : tileColors) color = {255, 255, 255};

    // 功能断言（覆盖/颜色/LOD/线宽）用**固定**线宽 0.20/1.5，不随代码默认值调参而变；
    // 代码默认值由 Config 测试与下面的预览断言覆盖。
    lw::Config::Render::River riverCfg = cfg.render.river;
    riverCfg.widthU = 0.20;
    riverCfg.minPx = 1.5;

    const lw::Map withRivers = makeMap(true, cfg);
    const lw::Map withoutRivers = makeMap(false, cfg);
    const auto renderWith = [&](const lw::Map& map, const lw::Config::Render::River& riverConfig,
                               const lw::render::Camera& cam) {
        lw::render::MapRenderer mapRenderer(renderer, cam, cfg.render.mountain, riverConfig);
        mapRenderer.bake(colors);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        mapRenderer.draw(map, tileColors);
        SDL_RenderPresent(renderer);
    };
    const auto render = [&](const lw::Map& map, const lw::Config::Render::River& river) {
        renderWith(map, river, camera);
    };
    // 世界 (1,0.5)（cell0 右边中点）→ 屏幕 (10,155)；世界 (1,1)（河顶点）→ (10,150)。
    constexpr int kEdgeMidX = 10, kEdgeMidY = 155;
    constexpr int kVertexX = 10, kVertexY = 150;

    render(withoutRivers, riverCfg);
    EXPECT_TRUE(isWhite(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format))
        << "无河时该点应是地块色";
    EXPECT_TRUE(isWhite(pixelAt(target, kVertexX, kVertexY), target->format));

    render(withRivers, riverCfg);
    EXPECT_TRUE(isDark(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format))
        << "河边中点应画成黑线";
    EXPECT_TRUE(isDark(pixelAt(target, kVertexX, kVertexY), target->format))
        << "河顶点应有圆角补圆";
    // 另一条河（cell0 上边，世界 y=1，x∈[0,1] → 屏幕 x∈[0,10]，y=150）：取 (5,150)。
    EXPECT_TRUE(isDark(pixelAt(target, 5, kVertexY), target->format))
        << "cell0 上边也应画成黑线";

    // LOD：单格像素 10 < minCellPx 20 → 整层不画。
    lw::Config::Render::River lod = riverCfg;
    lod.minCellPx = 20.0;
    render(withRivers, lod);
    EXPECT_TRUE(isWhite(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format))
        << "LOD 命中时不应画河";

    // 颜色配置生效：红色。
    lw::Config::Render::River red = riverCfg;
    red.color = {255, 0, 0};
    render(withRivers, red);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_GetRGBA(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format, &r, &g, &b, &a);
    EXPECT_GT(r, 180);
    EXPECT_LT(g, 80);
    EXPECT_LT(b, 80);

    // ---- 线宽 = 世界量：屏幕 px = max(minPx, widthU × cellPx)（§7.2 "随缩放 + 最小宽度"）----
    // 取 cell0 上边（世界 y=1 → 屏幕 y=150）上 x=5 一列（远离两端河顶点圆角）数黑像素 = 线宽。
    // 注意：cell0 右边（x=1）在缩小后屏幕 x 会靠近本列，故取 x=3 这一列量"上边"的线宽。
    constexpr int kProbeX = 3, kProbeY0 = 140, kProbeY1 = 160;
    // 用**显式** widthU=0.20、minPx=1.5 测线宽规则（不依赖代码默认值，默认值会随调参变化）：
    // cellPx = 10 → 2px。
    render(withRivers, riverCfg);
    const int widthAt1x = darkInColumn(target, kProbeX, kProbeY0, kProbeY1);
    EXPECT_EQ(widthAt1x, 2) << "zoom=1、widthU=0.20 时线宽应为 2px（= widthU × cellPx）";
    // 同一线宽的**等宽性**：同一条边上另一列 x=6 应完全一致。
    EXPECT_EQ(darkInColumn(target, 6, kProbeY0, kProbeY1), widthAt1x) << "同一条河边不应粗细不均";
    // 另一条河（cell0 右边，世界 x=1 → 屏幕 x=10，竖直）在 y=155 一行：线宽同为 2px。
    EXPECT_EQ(darkInRow(target, kEdgeMidY, 5, 15), widthAt1x) << "不同朝向的河边应等宽";

    // 放大 2×（锚点在 (5,150)，该世界点不动）：cellPx=20 → 4px（随缩放变粗）。
    lw::render::Camera zoomedIn = camera;
    zoomedIn.zoomAt(static_cast<double>(kProbeX), static_cast<double>(kVertexY), 2.0);
    renderWith(withRivers, riverCfg, zoomedIn);
    EXPECT_EQ(darkInColumn(target, kProbeX, kProbeY0, kProbeY1), 4) << "放大后线宽应同比变粗";
    // 缩小 0.25×：cellPx=2.5 → widthU×cellPx=0.5px < minPx=1.5 → 兜底为 1.5px（仍可见）。
    lw::render::Camera zoomedOut = camera;
    zoomedOut.zoomAt(static_cast<double>(kProbeX), static_cast<double>(kVertexY), 0.25);
    renderWith(withRivers, riverCfg, zoomedOut);
    const int widthAtMin = darkInColumn(target, kProbeX, kProbeY0, kProbeY1);
    EXPECT_GE(widthAtMin, 1) << "缩到很小时也应有最小宽度（否则会看不见）";
    EXPECT_LE(widthAtMin, 3) << "最小宽度不应把河画粗";

    // 缩略预览：河必须画出来（预览无纯黑地形色）；纹理入口也必须能建出来。
    // 同一条线宽规则：预览 cell = 20px/U → max(minPx, widthU×20)；把 widthU/minPx 调细应显著变细。
    SDL_Surface* previewWith =
        lw::render::renderMapPreviewSurface(withRivers, 320, cfg.render.river);
    ASSERT_NE(previewWith, nullptr);
    SDL_Surface* previewWithout =
        lw::render::renderMapPreviewSurface(withoutRivers, 320, cfg.render.river);
    ASSERT_NE(previewWithout, nullptr);
    const int previewDark = countDarkPixels(previewWith);
    EXPECT_GT(previewDark, 0) << "预览应画出河";
    EXPECT_EQ(countDarkPixels(previewWithout), 0) << "无河预览不应有黑像素";
    lw::Config::Render::River thin = cfg.render.river;
    thin.widthU = 0.05;
    thin.minPx = 1.0;
    SDL_Surface* previewThin = lw::render::renderMapPreviewSurface(withRivers, 320, thin);
    ASSERT_NE(previewThin, nullptr);
    EXPECT_GT(countDarkPixels(previewThin), 0) << "细线预览也应看得见河";
    EXPECT_LT(countDarkPixels(previewThin), previewDark) << "预览线宽也必须随配置变化";
    SDL_FreeSurface(previewThin);
    SDL_FreeSurface(previewWith);
    SDL_FreeSurface(previewWithout);
    SDL_Texture* previewTexture =
        lw::render::renderMapPreview(renderer, withRivers, 320, cfg.render.river);
    ASSERT_NE(previewTexture, nullptr);
    SDL_DestroyTexture(previewTexture);

    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(target);
    SDL_Quit();
}

// 河流折线（屏幕坐标）集合 + 点到折线的最近距离：用于"黑像素不得离河太远"的拐角瑕疵回归。
std::vector<std::array<double, 4>> riverScreenSegments(const lw::Map& map,
                                                       const lw::render::Camera& cam) {
    std::vector<std::array<double, 4>> segments;
    for (const lw::MapEdgeRef& ref : map.riverEdges()) {
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
        if (!map.geom().cellEdge(ref.cell, ref.edge, x0, y0, x1, y1)) continue;
        segments.push_back(
            {cam.toScreenX(x0), cam.toScreenY(y0), cam.toScreenX(x1), cam.toScreenY(y1)});
    }
    return segments;
}

double distanceToSegments(double px, double py, const std::vector<std::array<double, 4>>& segments) {
    double best = 1e18;
    for (const auto& s : segments) {
        const double dx = s[2] - s[0], dy = s[3] - s[1];
        const double len2 = dx * dx + dy * dy;
        double t = len2 > 0.0 ? ((px - s[0]) * dx + (py - s[1]) * dy) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        best = std::min(best, std::hypot(px - (s[0] + t * dx), py - (s[1] + t * dy)));
    }
    return best;
}

// 拐角瑕疵回归：**所有黑像素必须落在"距河折线 ≤ 半线宽 + 1px 光栅化余量"内**。
// 端点沿方向外扩半线宽会让拐角外侧多出 hw·(√2−1) 的方块凸起（本断言会红）；
// 正确的圆角连接 = 不扩端点的线段 ∪ 顶点处半径恰为 hw 的圆盘。
TEST(RiverRender, CornerJoinStaysWithinHalfWidth) {
    ASSERT_EQ(SDL_Init(0), 0) << SDL_GetError();
    SDL_Surface* target = SDL_CreateRGBSurfaceWithFormat(0, 800, 600, 32, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(target, nullptr) << SDL_GetError();
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    ASSERT_NE(renderer, nullptr) << SDL_GetError();

    const lw::Config cfg = lw::Config::loadFromJson("{}");
    lw::render::Camera camera;
    camera.configure(
        lw::math::ScreenTransform{static_cast<double>(kBlockSize), 0, static_cast<double>(kH)},
        800, 600, kW, kH);
    std::vector<std::array<int, 3>> colors(9, {255, 255, 255});
    std::array<std::array<int, 3>, lw::kFactionTotal> tileColors{};
    for (auto& color : tileColors) color = {255, 255, 255};
    const lw::Map map = makeMap(true, cfg);
    lw::Config::Render::River thick = cfg.render.river;
    // cellPx = 10 → 线宽 10px（hw=5px）：拐角方块凸起 ≈ 2px，足够被像素级断言抓到。
    thick.widthU = 1.0;
    thick.minPx = 1.0;
    lw::render::MapRenderer mapRenderer(renderer, camera, cfg.render.mountain, thick);
    mapRenderer.bake(colors);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    mapRenderer.draw(map, tileColors);
    SDL_RenderPresent(renderer);

    const auto segments = riverScreenSegments(map, camera);
    ASSERT_EQ(segments.size(), 2u);
    const double halfWidth = 0.5 * std::max(thick.minPx, thick.widthU * camera.cellPx());
    double maxDistance = 0.0;
    int darkCount = 0;
    for (int y = 0; y < 600; ++y) {
        for (int x = 0; x < 800; ++x) {
            if (!isDark(pixelAt(target, x, y), target->format)) continue;
            ++darkCount;
            maxDistance =
                std::max(maxDistance, distanceToSegments(x + 0.5, y + 0.5, segments));
        }
    }
    EXPECT_GT(darkCount, 0);
    EXPECT_LE(maxDistance, halfWidth + 1.0)
        << "黑像素离河折线超过半线宽 + 1px（拐角/端点的方块外扩过冲）";

    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(target);
    SDL_Quit();
}

}  // namespace
