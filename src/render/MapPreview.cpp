// MapPreview.cpp — 地图缩略预览实现（开发计划 P6）。
#include "render/MapPreview.h"

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <vector>

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
    // 2026-08 斜周期（33336 系）：世界为平行四边形（W.y/H.x 剪切）。若按世界坐标画格
    // 多边形，地图在缩略图中呈斜向细带（"只能看到地图一角"）。改为**旋转到常规矩形**：
    // gridPolygon 用 R^{-1}（格基逆矩阵，"旋转矩阵"）把每格映射到格坐标（矩形）域，再填充。
    const TilingGeom& tg = map.geom();
    const bool skew = tg.hasSkewedPeriod();

    // 逐格顶点（skew = 去剪切格坐标；否则世界坐标）→ 求坐标跨度（决定纹理尺寸/缩放）。
    std::vector<std::vector<std::array<double, 2>>> vxSt(static_cast<size_t>(map.cellCount()));
    std::vector<int> vn(static_cast<size_t>(map.cellCount()));
    double sxmin = 1e18, sxmax = -1e18, symin = 1e18, symax = -1e18;
    double tmpv[12], tmpg[12] = {0};
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        const int n = skew ? tg.gridPolygon(idx, tmpv, tmpg, 12) : tg.cellPolygon(idx, tmpv, tmpg, 12);
        vn[static_cast<size_t>(idx)] = n;
        if (n < 3) continue;
        auto& arr = vxSt[static_cast<size_t>(idx)];
        arr.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            arr.push_back({tmpv[i], tmpg[i]});
            sxmin = std::min(sxmin, tmpv[i]);
            sxmax = std::max(sxmax, tmpv[i]);
            symin = std::min(symin, tmpg[i]);
            symax = std::max(symax, tmpg[i]);
        }
    }
    if (sxmax <= sxmin) return nullptr;
    const double spanX = sxmax - sxmin, spanY = symax - symin;
    const double cell = std::max(1.0, static_cast<double>(previewW) / spanX);
    const int tw = std::max(1, static_cast<int>(std::lround(spanX * cell)));
    const int th = std::max(1, static_cast<int>(std::lround(spanY * cell)));
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        spdlog::error("MapPreview: surface create failed: {}", SDL_GetError());
        return nullptr;
    }
    Uint32* px = static_cast<Uint32*>(surf->pixels);

    // 扫描线填充：世界/格坐标 → 屏幕像素（y 翻转：坐标顶 = 图像顶）。offset 为 (sxmin,symin)。
    const auto fillPoly = [&](const std::vector<std::array<double, 2>>& v, Uint32 col) {
        const int n = static_cast<int>(v.size());
        double minY = v[0][1], maxY = v[0][1];
        for (int i = 1; i < n; ++i) {
            minY = std::min(minY, v[static_cast<size_t>(i)][1]);
            maxY = std::max(maxY, v[static_cast<size_t>(i)][1]);
        }
        const int sy0 = std::max(0, static_cast<int>(std::floor((th - 1.0 - (maxY - symin) * cell))));
        const int sy1 = std::min(th - 1, static_cast<int>(std::ceil((th - 1.0 - (minY - symin) * cell))));
        for (int sy = sy0; sy <= sy1; ++sy) {
            const double y = symin + (th - 0.5 - sy) / cell;  // 坐标 y（顶行 = 坐标高）
            double xmin = 1e18, xmax = -1e18;
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                const double yA = v[static_cast<size_t>(i)][1], yB = v[static_cast<size_t>(j)][1];
                if ((yA <= y && yB > y) || (yB <= y && yA > y)) {
                    const double t = (y - yA) / (yB - yA);
                    const double x = v[static_cast<size_t>(i)][0] +
                                     t * (v[static_cast<size_t>(j)][0] - v[static_cast<size_t>(i)][0]);
                    xmin = std::min(xmin, x);
                    xmax = std::max(xmax, x);
                }
            }
            if (xmax < xmin) continue;
            const int sx0 = std::max(0, static_cast<int>(std::floor((xmin - sxmin) * cell)));
            const int sx1 = std::min(tw - 1, static_cast<int>(std::ceil((xmax - sxmin) * cell)));
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
        const int n = vn[static_cast<size_t>(idx)];
        if (n >= 3) fillPoly(vxSt[static_cast<size_t>(idx)], col);
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (!tex) spdlog::error("MapPreview: texture create failed: {}", SDL_GetError());
    return tex;
}

}  // namespace lw::render
