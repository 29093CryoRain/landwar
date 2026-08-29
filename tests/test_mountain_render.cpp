// test_mountain_render.cpp — 按格面积缩放山纹的 SDL 软件渲染回归。
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include <SDL.h>

#include "core/Config.h"
#include "render/Camera.h"
#include "render/MapRenderer.h"
#include "world/Map.h"

namespace {

TEST(MountainRender, BakesAndDrawsAllTilingScales) {
    ASSERT_EQ(SDL_Init(0), 0) << SDL_GetError();
    SDL_Surface* target = SDL_CreateRGBSurfaceWithFormat(0, 800, 600, 32, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(target, nullptr) << SDL_GetError();
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    ASSERT_NE(renderer, nullptr) << SDL_GetError();

    const lw::Config cfg = lw::Config::loadFromJson("{}");
    const lw::TilingType types[] = {
        lw::TilingType::Square,    lw::TilingType::Hex,       lw::TilingType::Tri,
        lw::TilingType::Arch33336, lw::TilingType::Arch33434, lw::TilingType::Arch3464,
        lw::TilingType::Arch3636,  lw::TilingType::Arch31212, lw::TilingType::Arch4612,
        lw::TilingType::Arch488,   lw::TilingType::Laves3636, lw::TilingType::Laves31212,
        lw::TilingType::Laves4612, lw::TilingType::Laves488,   lw::TilingType::Laves33434,
        lw::TilingType::Laves33336, lw::TilingType::Laves3464};
    std::vector<std::array<int, 3>> colors(9, {100, 100, 100});
    std::array<std::array<int, 3>, lw::kFactionTotal> tileColors{};
    for (auto& color : tileColors) color = {160, 160, 160};

    for (const lw::TilingType type : types) {
        lw::Config::Map mapConfig = cfg.map;
        mapConfig.width = 48;
        mapConfig.height = 48;
        mapConfig.tiling = lw::tilingName(type);
        lw::Map map;
        map.configure(mapConfig);
        for (int idx = 0; idx < map.cellCount(); ++idx) {
            map.atIndex(idx).land = true;
            map.atIndex(idx).mountain = true;
            map.atIndex(idx).belongi = 1;
        }
        lw::render::Camera camera;
        camera.configure(lw::math::ScreenTransform{15.0, 0, map.worldHeight()}, 800, 600,
                         map.worldWidth(), map.worldHeight());
        {
            lw::render::MapRenderer mapRenderer(renderer, camera, cfg.render.mountain);
            mapRenderer.bake(colors);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            mapRenderer.draw(map, tileColors);
            SDL_RenderPresent(renderer);
        }
        EXPECT_EQ(SDL_GetError(), std::string()) << lw::tilingName(type);
        SDL_ClearError();
    }

    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(target);
    SDL_Quit();
}

}  // namespace
