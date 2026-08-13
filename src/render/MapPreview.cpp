// MapPreview.cpp — 地图缩略预览实现（开发计划 P6）。
#include "render/MapPreview.h"

#include <spdlog/spdlog.h>

#include <cmath>

#include "world/Map.h"

namespace lw::render {

namespace {
// 预览配色（中立地形）：海 / 陆 / 山 / 城。城盖山（同格有城有山 → 显城色）。
constexpr Uint8 kSeaR = 16, kSeaG = 32, kSeaB = 48;        // 深蓝黑海
constexpr Uint8 kLandR = 100, kLandG = 100, kLandB = 96;   // 中性灰陆
constexpr Uint8 kMtnR = 150, kMtnG = 128, kMtnB = 92;      // 褐山
constexpr Uint8 kCityR = 232, kCityG = 216, kCityB = 120;  // 亮黄城

}  // namespace

SDL_Texture* renderMapPreview(SDL_Renderer* ren, const Map& map, int previewW) {
    if (!ren) return nullptr;
    if (map.width() <= 0 || map.height() <= 0) return nullptr;
    const int cell = std::max(1, static_cast<int>(std::lround(
                                     static_cast<double>(previewW) / map.width())));
    const int tw = cell * map.width();
    const int th = cell * map.height();

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        spdlog::error("MapPreview: surface create failed: {}", SDL_GetError());
        return nullptr;
    }
    Uint32* px = static_cast<Uint32*>(surf->pixels);
    for (int j = 0; j < map.height(); ++j) {
        for (int i = 0; i < map.width(); ++i) {
            const MapCell& c = map.at(i, j);
            Uint8 r = kSeaR, g = kSeaG, b = kSeaB;
            if (c.land) {
                r = kLandR;
                g = kLandG;
                b = kLandB;
                if (c.mountain) {
                    r = kMtnR;
                    g = kMtnG;
                    b = kMtnB;
                }
                if (c.cityId >= 0) {
                    r = kCityR;
                    g = kCityG;
                    b = kCityB;
                }
            }
            const Uint32 col = SDL_MapRGBA(surf->format, r, g, b, 255);
            for (int cy = 0; cy < cell; ++cy) {
                // y 翻转：世界 y=0 在底部（与游戏内 toscreenY 一致），图像原点在左上 →
                // 世界行 j 画到图像底部行 (height-1-j)（修复预览上下颠倒，2026-08-06 用户反馈）。
                const int y0 = (map.height() - 1 - j) * cell + cy;
                for (int cx = 0; cx < cell; ++cx) {
                    px[static_cast<size_t>(y0) * tw + (i * cell + cx)] = col;
                }
            }
        }
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (!tex) spdlog::error("MapPreview: texture create failed: {}", SDL_GetError());
    return tex;
}

}  // namespace lw::render
