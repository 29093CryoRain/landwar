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

    if (map.tiling() == TilingType::Square) {
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

    // P12 六/三角：逐格凸多边形扫描线填充（真实密铺形状，杜绝"横纹/单朝向块"假象）。
    const TilingGeom& tg = map.geom();
    const double cell = std::max(1.0, static_cast<double>(previewW) / tg.worldWidth());
    const int tw = std::max(1, static_cast<int>(std::lround(tg.worldWidth() * cell)));
    const int th = std::max(1, static_cast<int>(std::lround(tg.worldHeight() * cell)));
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        spdlog::error("MapPreview: surface create failed: {}", SDL_GetError());
        return nullptr;
    }
    Uint32* px = static_cast<Uint32*>(surf->pixels);
    const auto fillPoly = [&](const double* vx, const double* vy, int n, Uint32 col) {
        double minY = vy[0], maxY = vy[0];
        for (int i = 1; i < n; ++i) {
            minY = std::min(minY, vy[i]);
            maxY = std::max(maxY, vy[i]);
        }
        const int sy0 = std::max(0, static_cast<int>(std::floor((th - 1.0 - maxY * cell))));
        const int sy1 = std::min(th - 1, static_cast<int>(std::ceil((th - 1.0 - minY * cell))));
        for (int sy = sy0; sy <= sy1; ++sy) {
            const double y = (th - 0.5 - sy) / cell;  // 世界 y（y 翻转：顶行 = 世界高）
            double xmin = 1e18, xmax = -1e18;
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                const double yA = vy[i], yB = vy[j];
                if ((yA <= y && yB > y) || (yB <= y && yA > y)) {
                    const double t = (y - yA) / (yB - yA);
                    const double x = vx[i] + t * (vx[j] - vx[i]);
                    xmin = std::min(xmin, x);
                    xmax = std::max(xmax, x);
                }
            }
            if (xmax < xmin) continue;
            const int sx0 = std::max(0, static_cast<int>(std::floor(xmin * cell)));
            const int sx1 = std::min(tw - 1, static_cast<int>(std::ceil(xmax * cell)));
            for (int sx = sx0; sx <= sx1; ++sx) px[static_cast<size_t>(sy) * tw + sx] = col;
        }
    };
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        const MapCell& c = map.atIndex(idx);
        Uint8 r = kSeaR, gg = kSeaG, b = kSeaB;
        if (c.land) {
            r = kLandR;
            gg = kLandG;
            b = kLandB;
            if (c.mountain) {
                r = kMtnR;
                gg = kMtnG;
                b = kMtnB;
            }
            if (c.cityId >= 0) {
                r = kCityR;
                gg = kCityG;
                b = kCityB;
            }
        }
        const Uint32 col = SDL_MapRGBA(surf->format, r, gg, b, 255);
        double vx[6], vy[6];
        const int n = tg.cellPolygon(idx, vx, vy, 6);
        if (n >= 3) fillPoly(vx, vy, n, col);
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (!tex) spdlog::error("MapPreview: texture create failed: {}", SDL_GetError());
    return tex;
}

}  // namespace lw::render
