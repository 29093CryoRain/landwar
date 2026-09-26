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

void appendLineQuad(std::vector<SDL_Vertex>& vertices, double x0, double y0, double x1, double y1,
                    const Camera& camera, const SDL_Color& color) {
    const double sx0 = camera.toScreenX(x0), sy0 = camera.toScreenY(y0);
    const double sx1 = camera.toScreenX(x1), sy1 = camera.toScreenY(y1);
    const double dx = sx1 - sx0, dy = sy1 - sy0;
    const double length = std::hypot(dx, dy);
    if (length <= 1e-9) return;
    constexpr double halfWidth = 0.6;
    const double nx = -dy / length * halfWidth, ny = dx / length * halfWidth;
    const SDL_FPoint p0{static_cast<float>(sx0 + nx), static_cast<float>(sy0 + ny)};
    const SDL_FPoint p1{static_cast<float>(sx1 + nx), static_cast<float>(sy1 + ny)};
    const SDL_FPoint p2{static_cast<float>(sx1 - nx), static_cast<float>(sy1 - ny)};
    const SDL_FPoint p3{static_cast<float>(sx0 - nx), static_cast<float>(sy0 - ny)};
    vertices.push_back({p0, color, {0.0f, 0.0f}});
    vertices.push_back({p1, color, {0.0f, 0.0f}});
    vertices.push_back({p2, color, {0.0f, 0.0f}});
    vertices.push_back({p0, color, {0.0f, 0.0f}});
    vertices.push_back({p2, color, {0.0f, 0.0f}});
    vertices.push_back({p3, color, {0.0f, 0.0f}});
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

void MapRenderer::draw(const Map& map, const std::vector<std::array<int, 3>>& tileColors,
                       const std::vector<std::vector<std::array<int, 3>>>& gradeColors) {
    ensureMountainScales(map);
    if (map.tiling() == TilingType::Square) {
        drawSquare(map, tileColors);
        return;
    }
    drawTiled(map, tileColors, gradeColors);
}

void MapRenderer::draw(const Map& map,
                       const std::array<std::array<int, 3>, kFactionTotal>& tileColors) {
    std::vector<std::array<int, 3>> colors(tileColors.begin(), tileColors.end());
    draw(map, colors);
}

void MapRenderer::drawGrid(const Map& map, const SDL_Color& color) {
    ensureTiledVertices(map);
    const TilingGeom& g = map.geom();
    const double vx0 = cam_.viewWorldX0(), vx1 = cam_.viewWorldX1();
    const double vy0 = cam_.viewWorldY0(), vy1 = cam_.viewWorldY1();
    int r0, r1, c0, c1;
    g.rowRange(vx0, vy0, vx1, vy1, r0, r1);
    std::vector<SDL_Vertex>& vertices = vertexScratch_;
    vertices.clear();
    if (vertices.capacity() < 6000) vertices.reserve(6000);
    const auto flush = [&]() {
        if (vertices.empty()) return;
        SDL_RenderGeometry(ren_, nullptr, vertices.data(), static_cast<int>(vertices.size()), nullptr,
                           0);
        vertices.clear();
    };
    for (int r = r0; r <= r1; ++r) {
        g.colRange(vx0, vx1, r, c0, c1);
        for (int c = c0; c <= c1; ++c) {
            for (int b = 0; b < g.baseCount(); ++b) {
                const int index = g.cellIndexAt(r, c, b);
                if (index < 0) continue;
                const int n = polyCache_.vertCount[static_cast<std::size_t>(index)];
                const std::size_t base = polyCache_.cellOffset[static_cast<std::size_t>(index)];
                for (int k = 0; k < n; ++k) {
                    const int next = (k + 1) % n;
                    appendLineQuad(vertices, polyCache_.wx[base + k], polyCache_.wy[base + k],
                                   polyCache_.wx[base + next], polyCache_.wy[base + next], cam_,
                                   color);
                }
                if (static_cast<int>(vertices.size()) >= 6000) flush();
            }
        }
    }
    flush();
}

void MapRenderer::drawSquare(const Map& map, const std::vector<std::array<int, 3>>& tileColors) {
    if (tileColors.empty()) return;
    const int colorGroups = static_cast<int>(tileColors.size());
    if (squareBatches_.size() != tileColors.size()) squareBatches_.resize(tileColors.size());
    for (auto& batch : squareBatches_) batch.clear();
    groupColors_.resize(tileColors.size());
    for (int f = 0; f < colorGroups; ++f)
        groupColors_[static_cast<size_t>(f)] = Renderer::toColor(tileColors[static_cast<size_t>(f)]);
    Renderer r(ren_);
    const int i0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldX0())), 0, map.width() - 1);
    const int i1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldX1())), 0, map.width() - 1);
    const int j0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldY0())), 0, map.height() - 1);
    const int j1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldY1())), 0, map.height() - 1);
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (!cell.land) continue;
            const int group = std::clamp(static_cast<int>(cell.belongi), 0, colorGroups - 1);
            squareBatches_[static_cast<size_t>(group)].push_back(cam_.cellRect(i, j));
        }
    for (int group = 0; group < colorGroups; ++group)
        if (!squareBatches_[static_cast<size_t>(group)].empty())
            r.fillRects(squareBatches_[static_cast<size_t>(group)],
                        groupColors_[static_cast<size_t>(group)]);

    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (cell.land && cell.mountain) drawMountain(map, j * map.width() + i, r);
        }
    drawRivers(map);
    drawBoundaryOutline(map);
}

void MapRenderer::ensureTiledVertices(const Map& map) {
    const TilingGeom& g = map.geom();
    const int cc = g.cellCount();
    if (polyCache_.tiling == g.type && polyCache_.cols == g.cols && polyCache_.rows == g.rows
        && polyCache_.cellCount == cc)
        return;
    polyCache_.tiling = g.type;
    polyCache_.cols = g.cols;
    polyCache_.rows = g.rows;
    polyCache_.cellCount = cc;
    polyCache_.vertCount.resize(static_cast<std::size_t>(cc));
    polyCache_.cellOffset.resize(static_cast<std::size_t>(cc + 1));
    std::size_t totalVerts = 0;
    double wxBuf[12], wyBuf[12];
    for (int idx = 0; idx < cc; ++idx) {
        const int n = g.cellPolygon(idx, wxBuf, wyBuf, 12);
        polyCache_.vertCount[static_cast<std::size_t>(idx)] = static_cast<std::uint8_t>(n);
        polyCache_.cellOffset[static_cast<std::size_t>(idx)] = totalVerts;
        totalVerts += static_cast<std::size_t>(n);
    }
    polyCache_.cellOffset[static_cast<std::size_t>(cc)] = totalVerts;
    polyCache_.wx.resize(totalVerts);
    polyCache_.wy.resize(totalVerts);
    for (int idx = 0; idx < cc; ++idx) {
        const int n = g.cellPolygon(idx, wxBuf, wyBuf, 12);
        const std::size_t base = polyCache_.cellOffset[static_cast<std::size_t>(idx)];
        for (int k = 0; k < n; ++k) {
            polyCache_.wx[base + static_cast<std::size_t>(k)] = wxBuf[k];
            polyCache_.wy[base + static_cast<std::size_t>(k)] = wyBuf[k];
        }
    }
}

void MapRenderer::drawTiled(
    const Map& map, const std::vector<std::array<int, 3>>& tileColors,
    const std::vector<std::vector<std::array<int, 3>>>& gradeColors) {
    if (tileColors.empty()) return;
    ensureTiledVertices(map);
    const TilingGeom& g = map.geom();
    Renderer r(ren_);
    const int colorGroups = static_cast<int>(tileColors.size());
    groupColors_.resize(tileColors.size());
    for (int f = 0; f < colorGroups; ++f)
        groupColors_[static_cast<size_t>(f)] = Renderer::toColor(tileColors[static_cast<size_t>(f)]);
    const bool graded = !gradeColors.empty();
    const int paletteSize = graded ? static_cast<int>(gradeColors.size()) : 0;
    const double vx0 = cam_.viewWorldX0(), vx1 = cam_.viewWorldX1();
    const double vy0 = cam_.viewWorldY0(), vy1 = cam_.viewWorldY1();
    int r0, r1, c0, c1;
    g.rowRange(vx0, vy0, vx1, vy1, r0, r1);
    constexpr int kBatchVerts = 6000;
    std::vector<SDL_Vertex>& verts = vertexScratch_;
    verts.clear();
    if (verts.capacity() < kBatchVerts) verts.reserve(kBatchVerts);
    const auto flush = [&]() {
        if (verts.empty()) return;
        SDL_RenderGeometry(ren_, nullptr, verts.data(), static_cast<int>(verts.size()), nullptr, 0);
        verts.clear();
    };
    for (int rr = r0; rr <= r1; ++rr) {
        g.colRange(vx0, vx1, rr, c0, c1);
        for (int cc = c0; cc <= c1; ++cc) {
            const int baseCount = g.baseCount();
            for (int b = 0; b < baseCount; ++b) {
                const int idx = g.cellIndexAt(rr, cc, b);
                if (idx < 0) continue;
                const MapCell& cell = map.atIndex(idx);
                if (!cell.land) continue;
                const int n = polyCache_.vertCount[static_cast<std::size_t>(idx)];
                const std::size_t base = polyCache_.cellOffset[static_cast<std::size_t>(idx)];
                const int faction = std::clamp(static_cast<int>(cell.belongi), 0, colorGroups - 1);
                const SDL_Color color = !graded
                                            ? groupColors_[static_cast<size_t>(faction)]
                                            : Renderer::toColor(gradeColors[static_cast<size_t>(
                                                  std::clamp(g.tileColorIndex(idx), 0, paletteSize - 1))]
                                                                      [static_cast<size_t>(faction)]);
                for (int k = 1; k + 1 < n; ++k) {
                    const int tri[3] = {0, k, k + 1};
                    for (int v = 0; v < 3; ++v) {
                        SDL_Vertex vertex;
                        vertex.position.x =
                            static_cast<float>(cam_.toScreenX(polyCache_.wx[base + tri[v]]));
                        vertex.position.y =
                            static_cast<float>(cam_.toScreenY(polyCache_.wy[base + tri[v]]));
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
    drawRivers(map);
    drawBoundaryOutline(map);
}

// 河流系统 §7.2：逐条规范河边画粗线段，再对每个河顶点补画一次实心圆（圆角连接）。
// **线宽是世界量**：屏幕线宽 px = max(minPx, widthU × cellPx())，故随缩放同比变化（放大变粗、
// 缩小变细）但永不细于 minPx。几何全用双精度屏幕坐标 + 端点外扩 + 半径恰为 px/2 的圆盘，
// 消除整数取整导致的"毛刺/粗细不均"。纯渲染，不消耗任何 RNG。
void MapRenderer::drawRivers(const Map& map) {
    const std::vector<MapEdgeRef>& edges = map.riverEdges();
    if (edges.empty()) return;
    const double cellPx = cam_.cellPx();
    if (riverConfig_.minCellPx > 0.0 && cellPx < riverConfig_.minCellPx) return;
    const double widthPx = std::max(riverConfig_.minPx, riverConfig_.widthU * cellPx);
    if (!(widthPx > 0.0)) return;
    const SDL_Color color = Renderer::toColor(riverConfig_.color);
    const TilingGeom& g = map.geom();
    Renderer r(ren_);
    // 视野剔除的保守世界半径：线宽折算 + 1 格余量（端点圆角）。
    const double worldRadius = (cellPx > 1e-9 ? widthPx / cellPx : 1.0) + 1.0;

    std::vector<std::uint64_t> vertexKeys;
    vertexKeys.reserve(edges.size() * 2);
    for (const MapEdgeRef& ref : edges) {
        int vA = -1, vB = -1;
        if (!g.cellEdgeVertices(ref.cell, ref.edge, vA, vB)) continue;
        const std::uint64_t keyA = g.vertexKey(ref.cell, vA);
        const std::uint64_t keyB = g.vertexKey(ref.cell, vB);
        if (keyA != 0) vertexKeys.push_back(keyA);
        if (keyB != 0) vertexKeys.push_back(keyB);
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
        if (!g.cellEdge(ref.cell, ref.edge, x0, y0, x1, y1)) continue;
        if (!render::isVisibleOnScreen(cam_, 0.5 * (x0 + x1), 0.5 * (y0 + y1), worldRadius))
            continue;
        r.fillThickSegmentF(cam_.toScreenX(x0), cam_.toScreenY(y0), cam_.toScreenX(x1),
                            cam_.toScreenY(y1), widthPx, color);
    }
    if (vertexKeys.empty()) return;
    std::sort(vertexKeys.begin(), vertexKeys.end());
    vertexKeys.erase(std::unique(vertexKeys.begin(), vertexKeys.end()), vertexKeys.end());
    const double radius = widthPx * 0.5;  // 圆角半径 = 半线宽（与线段边缘严格相切）
    for (const std::uint64_t key : vertexKeys) {
        int cell = -1, vert = -1;
        if (!g.vertexFromKey(key, cell, vert)) continue;
        double wx = 0.0, wy = 0.0;
        g.cellVertex(cell, vert, wx, wy);
        if (!render::isVisibleOnScreen(cam_, wx, wy, worldRadius)) continue;
        r.fillDiscF(cam_.toScreenX(wx), cam_.toScreenY(wy), radius, color);
    }
}

void MapRenderer::drawBoundaryOutline(const Map& map) {
    const SDL_Color outline = {150, 150, 150, 255};
    Renderer r(ren_);
    const TilingGeom& g = map.geom();
    const int thick = 2;
    // 全密铺统一：逐格找"对侧无邻格"的边（地图边界边）画粗线段。方/六/三/半正/Laves 同一条路径。
    const double vx0 = cam_.viewWorldX0(), vx1 = cam_.viewWorldX1();
    const double vy0 = cam_.viewWorldY0(), vy1 = cam_.viewWorldY1();
    int r0, r1, c0, c1;
    g.rowRange(vx0, vy0, vx1, vy1, r0, r1);
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
