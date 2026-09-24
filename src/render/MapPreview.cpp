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

// 点到线段的距离（"胶囊"判据：距离 <= r 即覆盖 → 天然的圆角端帽/连接，无折角缺口）。
double distanceToSegment(double px, double py, double x0, double y0, double x1, double y1) {
    const double dx = x1 - x0, dy = y1 - y0;
    const double d2 = dx * dx + dy * dy;
    double t = 0.0;
    if (d2 > 1e-12) t = std::clamp(((px - x0) * dx + (py - y0) * dy) / d2, 0.0, 1.0);
    return std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}

// 河流系统 §7.3：把河边画到缩略图上（**与世界视图同一线宽规则**：屏幕宽 px =
// max(minPx, widthU × cell)，cell = 每世界单位在预览里的像素数）。每边按"胶囊"（点到线段
// 距离 <= px/2）逐像素判定 → 等宽、无折角缺口，也不用再补圆点。
// 用 gridVertex（非斜周期 = 世界坐标）保证与上面的多边形填充同一坐标系。
// 变换：sx = (x - ox)·cell，sy = (th - 0.5) - (y - oy)·cell（与 fillPoly 的 y 翻转一致）。
void plotPreviewRivers(Uint32* px, int tw, int th, const Map& map, double ox, double oy,
                       double cell, Uint32 col, const Config::Render::River& cfg) {
    if (map.riverEdges().empty()) return;
    const TilingGeom& g = map.geom();
    const double half = 0.5 * std::max(cfg.minPx, cfg.widthU * cell);
    if (!(half > 0.0)) return;
    for (const MapEdgeRef& ref : map.riverEdges()) {
        int vA = -1, vB = -1;
        if (!g.cellEdgeVertices(ref.cell, ref.edge, vA, vB)) continue;
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
        g.gridVertex(ref.cell, vA, x0, y0);
        g.gridVertex(ref.cell, vB, x1, y1);
        const double sx0 = (x0 - ox) * cell, sy0 = (th - 0.5) - (y0 - oy) * cell;
        const double sx1 = (x1 - ox) * cell, sy1 = (th - 0.5) - (y1 - oy) * cell;
        const int bx0 = std::max(0, static_cast<int>(std::floor(std::min(sx0, sx1) - half)));
        const int bx1 = std::min(tw - 1, static_cast<int>(std::ceil(std::max(sx0, sx1) + half)));
        const int by0 = std::max(0, static_cast<int>(std::floor(std::min(sy0, sy1) - half)));
        const int by1 = std::min(th - 1, static_cast<int>(std::ceil(std::max(sy0, sy1) + half)));
        for (int sy = by0; sy <= by1; ++sy)
            for (int sx = bx0; sx <= bx1; ++sx)
                if (distanceToSegment(sx + 0.5, sy + 0.5, sx0, sy0, sx1, sy1) <= half)
                    px[static_cast<std::size_t>(sy) * tw + sx] = col;
    }
}

}  // namespace

SDL_Texture* renderMapPreview(SDL_Renderer* ren, const Map& map, int previewW,
                              const Config::Render::River& river) {
    if (!ren) return nullptr;
    SDL_Surface* surf = renderMapPreviewSurface(map, previewW, river);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (!tex) spdlog::error("MapPreview: texture create failed: {}", SDL_GetError());
    return tex;
}

SDL_Surface* renderMapPreviewSurface(const Map& map, int previewW,
                                     const Config::Render::River& river) {
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
        // 河流系统 §7.3：方形单位格世界坐标即 [0,width]x[0,height] → 原点 (0,0)。
        plotPreviewRivers(px, tw, th, map, 0.0, 0.0, static_cast<double>(cell),
                          SDL_MapRGBA(surf->format, 0, 0, 0, 255), river);
        return surf;
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
    // 河流系统 §7.3：与上面的多边形同一坐标系（斜周期 = 格坐标）与同一原点。
    plotPreviewRivers(px, tw, th, map, sxmin, symin, cell,
                      SDL_MapRGBA(surf->format, 0, 0, 0, 255), river);
    return surf;
}

}  // namespace lw::render
