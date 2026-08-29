// MapRenderer.cpp — 地图网格渲染实现（翻新计划 Phase 7 §2.9；P5 山地/城市改版）。
#include "render/MapRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "core/GameDefs.h"
#include "render/RendererUtil.h"

namespace lw::render {

namespace {

constexpr int kColorGroups = kPlayerFactionCount + 1;

// mountain.png 是 128×128 的四段折线。预烘焙时按格面积缩放端点，
// 但 strokeWidthPx 直接使用目标 surface 像素值，因而线宽不随面积缩放。
struct Segment {
    double x0, y0, x1, y1;
};

double pointSegmentDistance(const Segment& segment, double x, double y) {
    const double dx = segment.x1 - segment.x0, dy = segment.y1 - segment.y0;
    const double length2 = dx * dx + dy * dy;
    const double t = length2 <= 1e-12
                         ? 0.0
                         : std::clamp(((x - segment.x0) * dx + (y - segment.y0) * dy) / length2,
                                      0.0, 1.0);
    return std::hypot(x - (segment.x0 + t * dx), y - (segment.y0 + t * dy));
}

SDL_Surface* bakeMountainMask(const Config::Render::Mountain& config, double scale) {
    const int width = std::max(1, static_cast<int>(std::lround(config.sourceWidth * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(config.sourceHeight * scale)));
    SDL_Surface* surface =
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;

    const Uint32 transparent = SDL_MapRGBA(surface->format, 255, 255, 255, 0);
    SDL_FillRect(surface, nullptr, transparent);
    const double sourceCx = config.sourceWidth * 0.5;
    const double sourceCy = config.sourceHeight * 0.5;
    std::vector<Segment> segments;
    segments.reserve(config.segments.size());
    for (const auto& raw : config.segments) {
        segments.push_back({(raw[0] - sourceCx) * scale + width * 0.5,
                            (raw[1] - sourceCy) * scale + height * 0.5,
                            (raw[2] - sourceCx) * scale + width * 0.5,
                            (raw[3] - sourceCy) * scale + height * 0.5});
    }

    const double halfStroke = config.strokeWidthPx * 0.5;
    constexpr double kAntiAliasPixels = 1.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double distance = std::numeric_limits<double>::max();
            for (const auto& segment : segments)
                distance = std::min(distance, pointSegmentDistance(
                                                   segment, x + 0.5, y + 0.5));
            if (distance > halfStroke + kAntiAliasPixels) continue;
            const Uint8 alpha = static_cast<Uint8>(std::clamp(
                (halfStroke + kAntiAliasPixels - distance) / kAntiAliasPixels * 255.0, 0.0,
                255.0));
            auto* pixel = static_cast<Uint32*>(static_cast<void*>(
                static_cast<Uint8*>(surface->pixels) + static_cast<std::size_t>(y) * surface->pitch +
                static_cast<std::size_t>(x) * sizeof(Uint32)));
            *pixel = SDL_MapRGBA(surface->format, 255, 255, 255, alpha);
        }
    }
    return surface;
}

}  // namespace

void MapRenderer::releaseMountainScales() {
    mountainScales_.clear();
}

void MapRenderer::bake(const std::vector<std::array<int, 3>>& factionColors) {
    mountainColors_.clear();
    mountainColors_.reserve(factionColors.size());
    for (const auto& color : factionColors)
        mountainColors_.push_back(scaledColor(color, mountainConfig_.colorDarken));
    releaseMountainScales();
}

void MapRenderer::ensureMountainScales(const Map& map) {
    if (!mountainScales_.empty() || mountainColors_.empty()) return;
    const TilingGeom& geom = map.geom();
    std::vector<double> scales;
    scales.reserve(static_cast<std::size_t>(geom.baseCount()));
    for (int index = 0; index < map.cellCount(); ++index) {
        const double area = geom.cellArea(index);
        const double scale = std::sqrt(std::max(0.0, area) / mountainConfig_.areaReference);
        bool seen = false;
        for (double existing : scales) {
            if (std::fabs(existing - scale) <= 1e-6) {
                seen = true;
                break;
            }
        }
        if (!seen) scales.push_back(scale);
    }
    std::sort(scales.begin(), scales.end());
    for (double scale : scales) {
        MountainScale target;
        target.scale = scale;
        SDL_Surface* mask = bakeMountainMask(mountainConfig_, scale);
        if (!mask) continue;
        const int halfWidth = mask->w / 2;
        const int quarterWidth = mask->w / 4;
        target.textures.loadSurface(ren_, mask, mountainColors_, TintMode::Multiply, false, 0,
                                    {halfWidth, quarterWidth});
        SDL_FreeSurface(mask);
        mountainScales_.push_back(std::move(target));
    }
}

void MapRenderer::drawMountain(const Map& map, int index, Renderer& renderer) {
    if (mountainScales_.empty()) return;
    const TilingGeom& geom = map.geom();
    const double area = geom.cellArea(index);
    const double wanted = std::sqrt(std::max(0.0, area) / mountainConfig_.areaReference);
    const MountainScale* selected = &mountainScales_.front();
    for (const auto& candidate : mountainScales_)
        if (std::fabs(candidate.scale - wanted) < std::fabs(selected->scale - wanted))
            selected = &candidate;
    if (selected->textures.count() <= 0) return;

    double cx, cy;
    geom.cellCenter(index, cx, cy);
    const int dstW = std::max(1, static_cast<int>(std::lround(
                                      mountainConfig_.sourceWidth * selected->scale * cam_.cellPx() /
                                      mountainConfig_.sourceWidth)));
    const int dstH = std::max(1, static_cast<int>(std::lround(
                                      mountainConfig_.sourceHeight * selected->scale * cam_.cellPx() /
                                      mountainConfig_.sourceWidth)));
    const int faction = std::clamp(static_cast<int>(map.atIndex(index).belongi), 0,
                                   selected->textures.count() - 1);
    const int sizeIndex = selected->textures.pickSizeIndex(dstW);
    const SDL_Rect source{0, 0, selected->textures.width(sizeIndex),
                          selected->textures.height(sizeIndex)};
    renderer.drawSpriteCenteredRect(selected->textures.texture(faction, sizeIndex), source,
                                    cam_.toScreenXi(cx), cam_.toScreenYi(cy), dstW, dstH);
}

void MapRenderer::draw(const Map& map,
                       const std::array<std::array<int, 3>, kFactionTotal>& tileColors,
                       const std::vector<std::array<std::array<int, 3>, kFactionTotal>>& gradeColors) {
    ensureMountainScales(map);
    if (map.tiling() == TilingType::Square) {
        drawSquare(map, tileColors);
        return;
    }
    drawTiled(map, tileColors, gradeColors);
}

void MapRenderer::drawSquare(
    const Map& map, const std::array<std::array<int, 3>, kFactionTotal>& tileColors) {
    std::array<std::vector<SDL_Rect>, kColorGroups> batches;
    std::array<SDL_Color, kColorGroups> groupColor;
    for (int f = 0; f < kColorGroups; ++f)
        groupColor[static_cast<size_t>(f)] = Renderer::toColor(tileColors[static_cast<size_t>(f)]);
    Renderer r(ren_);
    const int i0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldX0())), 0, map.width() - 1);
    const int i1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldX1())), 0, map.width() - 1);
    const int j0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldY0())), 0, map.height() - 1);
    const int j1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldY1())), 0, map.height() - 1);
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (!cell.land) continue;
            const int group = std::clamp(static_cast<int>(cell.belongi), 0, kColorGroups - 1);
            batches[static_cast<size_t>(group)].push_back(cam_.cellRect(i, j));
        }
    for (int group = 0; group < kColorGroups; ++group)
        if (!batches[static_cast<size_t>(group)].empty())
            r.fillRects(batches[static_cast<size_t>(group)], groupColor[static_cast<size_t>(group)]);

    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (cell.land && cell.mountain) drawMountain(map, j * map.width() + i, r);
        }
    drawBoundaryOutline(map);
}

void MapRenderer::drawTiled(
    const Map& map, const std::array<std::array<int, 3>, kFactionTotal>& tileColors,
    const std::vector<std::array<std::array<int, 3>, kFactionTotal>>& gradeColors) {
    const TilingGeom& g = map.geom();
    Renderer r(ren_);
    SDL_Color groupColorArr[kColorGroups];
    for (int f = 0; f < kColorGroups; ++f)
        groupColorArr[f] = Renderer::toColor(tileColors[static_cast<size_t>(f)]);
    const bool graded = !gradeColors.empty();
    const int paletteSize = graded ? static_cast<int>(gradeColors.size()) : 0;
    const double vx0 = cam_.viewWorldX0(), vx1 = cam_.viewWorldX1();
    const double vy0 = cam_.viewWorldY0(), vy1 = cam_.viewWorldY1();
    int r0, r1, c0, c1;
    g.rowRange(vy0, vy1, r0, r1);
    constexpr int kBatchVerts = 6000;
    std::vector<SDL_Vertex> verts;
    verts.reserve(kBatchVerts);
    const auto flush = [&]() {
        if (verts.empty()) return;
        SDL_RenderGeometry(ren_, nullptr, verts.data(), static_cast<int>(verts.size()), nullptr, 0);
        verts.clear();
    };
    double wx[12], wy[12];
    for (int rr = r0; rr <= r1; ++rr) {
        g.colRange(vx0, vx1, rr, c0, c1);
        for (int cc = c0; cc <= c1; ++cc) {
            const int baseCount = g.baseCount();
            for (int b = 0; b < baseCount; ++b) {
                const int idx = g.cellIndexAt(rr, cc, b);
                if (idx < 0) continue;
                const MapCell& cell = map.atIndex(idx);
                if (!cell.land) continue;
                const int n = g.cellPolygon(idx, wx, wy, 12);
                const int faction = std::clamp(static_cast<int>(cell.belongi), 0, kColorGroups - 1);
                const SDL_Color color = !graded
                                            ? groupColorArr[static_cast<size_t>(faction)]
                                            : Renderer::toColor(gradeColors[static_cast<size_t>(
                                                  std::clamp(g.tileColorIndex(idx), 0, paletteSize - 1))]
                                                                    [static_cast<size_t>(faction)]);
                for (int k = 1; k + 1 < n; ++k) {
                    const int tri[3] = {0, k, k + 1};
                    for (int v = 0; v < 3; ++v) {
                        SDL_Vertex vertex;
                        vertex.position.x = static_cast<float>(cam_.toScreenX(wx[tri[v]]));
                        vertex.position.y = static_cast<float>(cam_.toScreenY(wy[tri[v]]));
                        vertex.color = color;
                        vertex.tex_coord = {0.0f, 0.0f};
                        verts.push_back(vertex);
                    }
                }
                if (static_cast<int>(verts.size()) >= kBatchVerts) flush();
            }
        }
    }
    flush();

    for (int rr = r0; rr <= r1; ++rr) {
        g.colRange(vx0, vx1, rr, c0, c1);
        for (int cc = c0; cc <= c1; ++cc) {
            const int baseCount = g.baseCount();
            for (int b = 0; b < baseCount; ++b) {
                const int idx = g.cellIndexAt(rr, cc, b);
                if (idx < 0) continue;
                const MapCell& cell = map.atIndex(idx);
                if (cell.land && cell.mountain) drawMountain(map, idx, r);
            }
        }
    }
    drawBoundaryOutline(map);
}

void MapRenderer::drawBoundaryOutline(const Map& map) {
    const SDL_Color outline = {150, 150, 150, 255};
    Renderer r(ren_);
    const TilingGeom& g = map.geom();
    const int thick = 2;
    if (g.type == TilingType::Square) {
        const int x0 = cam_.toScreenXi(0.0), y0 = cam_.toScreenYi(0.0);
        const int x1 = cam_.toScreenXi(static_cast<double>(map.width()));
        const int y1 = cam_.toScreenYi(static_cast<double>(map.height()));
        r.fillThickSegment(x0, y0, x1, y0, thick, outline);
        r.fillThickSegment(x1, y0, x1, y1, thick, outline);
        r.fillThickSegment(x1, y1, x0, y1, thick, outline);
        r.fillThickSegment(x0, y1, x0, y0, thick, outline);
        return;
    }
    const double vx0 = cam_.viewWorldX0(), vx1 = cam_.viewWorldX1();
    const double vy0 = cam_.viewWorldY0(), vy1 = cam_.viewWorldY1();
    int r0, r1, c0, c1;
    g.rowRange(vy0, vy1, r0, r1);
    for (int rr = r0; rr <= r1; ++rr) {
        g.colRange(vx0, vx1, rr, c0, c1);
        for (int cc = c0; cc <= c1; ++cc) {
            const int baseCount = g.baseCount();
            for (int b = 0; b < baseCount; ++b) {
                const int idx = g.cellIndexAt(rr, cc, b);
                if (idx < 0) continue;
                for (int k = 0; k < g.neighborCount(idx); ++k) {
                    if (g.neighbor(idx, k) >= 0) continue;
                    double x0, y0, x1, y1;
                    if (!g.cellEdge(idx, k, x0, y0, x1, y1)) continue;
                    r.fillThickSegment(cam_.toScreenXi(x0), cam_.toScreenYi(y0),
                                       cam_.toScreenXi(x1), cam_.toScreenYi(y1), thick, outline);
                }
            }
        }
    }
}

}  // namespace lw::render
