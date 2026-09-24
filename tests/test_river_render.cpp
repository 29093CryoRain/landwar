// test_river_render.cpp — 河流渲染的 SDL 软件渲染回归（《河流系统开发文档》§7.2/§7.3）。
// 断言：有河 → 河边中点/河顶点出现黑线黑点；无河 → 同点仍是地块色；LOD 命中整层不画；
// 颜色/线宽配置生效；缩略预览也画出河。
#include <gtest/gtest.h>

#include <array>
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

    const lw::Map withRivers = makeMap(true, cfg);
    const lw::Map withoutRivers = makeMap(false, cfg);
    const auto render = [&](const lw::Map& map, const lw::Config::Render::River& riverCfg) {
        lw::render::MapRenderer mapRenderer(renderer, camera, cfg.render.mountain, riverCfg);
        mapRenderer.bake(colors);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        mapRenderer.draw(map, tileColors);
        SDL_RenderPresent(renderer);
    };
    // 世界 (1,0.5)（cell0 右边中点）→ 屏幕 (10,155)；世界 (1,1)（河顶点）→ (10,150)。
    constexpr int kEdgeMidX = 10, kEdgeMidY = 155;
    constexpr int kVertexX = 10, kVertexY = 150;

    render(withoutRivers, cfg.render.river);
    EXPECT_TRUE(isWhite(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format))
        << "无河时该点应是地块色";
    EXPECT_TRUE(isWhite(pixelAt(target, kVertexX, kVertexY), target->format));

    render(withRivers, cfg.render.river);
    EXPECT_TRUE(isDark(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format))
        << "河边中点应画成黑线";
    EXPECT_TRUE(isDark(pixelAt(target, kVertexX, kVertexY), target->format))
        << "河顶点应有圆角补圆";
    // 另一条河（cell0 上边，世界 y=1，x∈[0,1] → 屏幕 x∈[0,10]，y=150）：取 (5,150)。
    EXPECT_TRUE(isDark(pixelAt(target, 5, kVertexY), target->format))
        << "cell0 上边也应画成黑线";

    // LOD：单格像素 10 < minCellPx 20 → 整层不画。
    lw::Config::Render::River lod = cfg.render.river;
    lod.minCellPx = 20.0;
    render(withRivers, lod);
    EXPECT_TRUE(isWhite(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format))
        << "LOD 命中时不应画河";

    // 颜色与线宽配置生效：红色 + 5px（覆盖更宽）。
    lw::Config::Render::River red = cfg.render.river;
    red.color = {255, 0, 0};
    red.thicknessPx = 5.0;
    render(withRivers, red);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_GetRGBA(pixelAt(target, kEdgeMidX, kEdgeMidY), target->format, &r, &g, &b, &a);
    EXPECT_GT(r, 180);
    EXPECT_LT(g, 80);
    EXPECT_LT(b, 80);

    // 缩略预览：河必须画出来（预览无纯黑地形色）；纹理入口也必须能建出来。
    SDL_Surface* previewWith = lw::render::renderMapPreviewSurface(withRivers, 320);
    ASSERT_NE(previewWith, nullptr);
    SDL_Surface* previewWithout = lw::render::renderMapPreviewSurface(withoutRivers, 320);
    ASSERT_NE(previewWithout, nullptr);
    EXPECT_GT(countDarkPixels(previewWith), 0) << "预览应画出河";
    EXPECT_EQ(countDarkPixels(previewWithout), 0) << "无河预览不应有黑像素";
    SDL_FreeSurface(previewWith);
    SDL_FreeSurface(previewWithout);
    SDL_Texture* previewTexture = lw::render::renderMapPreview(renderer, withRivers, 320);
    ASSERT_NE(previewTexture, nullptr);
    SDL_DestroyTexture(previewTexture);

    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(target);
    SDL_Quit();
}

}  // namespace
