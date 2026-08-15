// MapRenderer.cpp — 地图网格渲染实现（翻新计划 Phase 7 §2.9；P5 山地/城市改版）。
#include "render/MapRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/GameDefs.h"
#include "render/RendererUtil.h"

namespace lw::render {

namespace {
constexpr int kTerrainTexSize = 128;    // data/mountain.png、data/city.png 源图尺寸
// 颜色组数 = 势力数(9，含中立0) —— 陆地基色按同色格聚合的批次上限。
constexpr int kColorGroups = kPlayerFactionCount + 1;  // 9
}  // namespace

void MapRenderer::bake(const std::vector<std::array<int, 3>>& factionColors) {
    // 白底透明线条源图 → 目标色（Multiply 整体 tint，同玩家指示贴图管线）。
    // 山 = 近黑但不纯黑（8% 势力色）。计划草案示例数值（山 0.92）与 mixColor 语义
    // （rate=势力色占比）相抵触，按文字描述实现（近黑），已记入开发计划 P5 完成记录。
    // P13：城贴图迁出到 CityRenderer（基建格/等级图标/细线围区），此处只烘焙山。
    std::vector<std::array<int, 3>> mtn;
    mtn.reserve(factionColors.size());
    for (const auto& c : factionColors) mtn.push_back(scaledColor(c, 0.08));
    // LOD 多尺寸（细节改进 2026-08）：预烘焙 128/64/32/16 档，缩放小时按屏上格大小选档，
    // 规避 128px 线条图被 GPU 降采样糊成几像素（见 drawOverlay 的 pickSizeIndex）。
    const std::vector<int> lodSizes = {kTerrainTexSize, 64, 32, 16};
    mountain_.load(ren_, "data/mountain.png", mtn, TintMode::Multiply, /*grayscaleFirst=*/false,
                   /*preserveWhite=*/0, lodSizes);
}

void MapRenderer::draw(const Map& map,
                       const std::array<std::array<int, 3>, kFactionTotal>& tileColors) {
    if (map.tiling() == TilingType::Square) {
        drawSquare(map, tileColors);
        return;
    }
    drawTiled(map, tileColors);
}

// 正方形（现状路径，零改动）：矩形批次 + 山贴图。
void MapRenderer::drawSquare(const Map& map,
                             const std::array<std::array<int, 3>, kFactionTotal>& tileColors) {
    // 陆地基色（山/城同底色）按势力聚合矩形批次 → 一次 SDL_RenderFillRects 画一组。
    // 双色系统（视觉工程改进 ⑫）：填色 = 调色板 tileColor(id)（主:副:白 加权平均），
    // 取代旧 mix_color(势力色, 白, 0.6)。
    std::array<std::vector<SDL_Rect>, kColorGroups> batches;
    std::array<SDL_Color, kColorGroups> groupColor;
    for (int f = 0; f < kColorGroups; ++f)
        groupColor[static_cast<size_t>(f)] = Renderer::toColor(tileColors[static_cast<size_t>(f)]);
    Renderer r(ren_);
    // 视野剔除（缩放/平移时只绘制可见范围，避免扫描全图 9975 格）。
    const int i0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldX0())), 0, map.width() - 1);
    const int i1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldX1())), 0, map.width() - 1);
    const int j0 = std::clamp(static_cast<int>(std::floor(cam_.viewWorldY0())), 0, map.height() - 1);
    const int j1 = std::clamp(static_cast<int>(std::ceil(cam_.viewWorldY1())), 0, map.height() - 1);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (!cell.land) continue;  // 海 = 背景黑（原版零宽空操作）
            const int g = std::clamp(static_cast<int>(cell.belongi), 0, kColorGroups - 1);
            batches[static_cast<size_t>(g)].push_back(cam_.cellRect(i, j));
        }
    }
    for (int g = 0; g < kColorGroups; ++g) {
        if (!batches[static_cast<size_t>(g)].empty())
            r.fillRects(batches[static_cast<size_t>(g)], groupColor[static_cast<size_t>(g)]);
    }

    // 叠加山线条贴图（P13：城市贴图迁出到 CityRenderer，本层只画山）。
    if (mountain_.count() == 0) return;  // 贴图缺失/未烘焙 → 跳过叠加（保底可运行）
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const MapCell& cell = map.at(i, j);
            if (!cell.land || !cell.mountain) continue;
            const SDL_Rect rc = cam_.cellRect(i, j);
            const int size = std::max(rc.w, rc.h);
            if (size <= 0) continue;
            const int ci = std::clamp(static_cast<int>(cell.belongi), 0, mountain_.count() - 1);
            const int si = mountain_.pickSizeIndex(size);  // LOD：按屏上格大小选预烘焙档位
            SDL_Texture* tex = mountain_.texture(ci, si);
            if (!tex) continue;
            const int srcSize = mountain_.sizePixel(si);
            r.drawSpriteCentered(tex, {0, 0, srcSize, srcSize}, rc.x + rc.w / 2, rc.y + rc.h / 2,
                                 size);
        }
    }
    drawBoundaryOutline(map);
}

// P12 六/三角：逐格多边形填充（扇形三角化，SDL_RenderGeometry）+ 边界灰线 + 山贴图。
void MapRenderer::drawTiled(const Map& map,
                            const std::array<std::array<int, 3>, kFactionTotal>& tileColors) {
    const TilingGeom& g = map.geom();
    Renderer r(ren_);
    SDL_Color groupColorArr[kColorGroups];
    for (int f = 0; f < kColorGroups; ++f)
        groupColorArr[f] = Renderer::toColor(tileColors[static_cast<size_t>(f)]);
    const double vx0 = cam_.viewWorldX0(), vx1 = cam_.viewWorldX1();
    const double vy0 = cam_.viewWorldY0(), vy1 = cam_.viewWorldY1();
    int r0, r1, c0, c1;
    g.rowRange(vy0, vy1, r0, r1);
    // 顶点分批提交：驱动几何顶点缓冲存在上限（~65535），全图 80×80 六边形 ≈ 7.7 万顶点
    // 超出会被截断 → 画面出现固定"纯黑区域"（2026-08 反馈修复）。
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
            const int oCount = (g.type == TilingType::Tri) ? 2 : 1;
            for (int o = 0; o < oCount; ++o) {
                const int idx = (g.type == TilingType::Tri) ? 2 * (rr * g.cols + cc) + o
                                                            : rr * g.cols + cc;
                const MapCell& cell = map.atIndex(idx);
                if (!cell.land) continue;
                const int n = g.cellPolygon(idx, wx, wy, 12);
                const SDL_Color col = groupColorArr[std::clamp(static_cast<int>(cell.belongi), 0,
                                                               kColorGroups - 1)];
                // 扇形三角化：0-1-2, 0-2-3, ...（凸多边形有效）。
                for (int k = 1; k + 1 < n; ++k) {
                    const int tri[3] = {0, k, k + 1};
                    for (int v = 0; v < 3; ++v) {
                        SDL_Vertex sv;
                        sv.position.x = static_cast<float>(cam_.toScreenX(wx[tri[v]]));
                        sv.position.y = static_cast<float>(cam_.toScreenY(wy[tri[v]]));
                        sv.color = col;
                        sv.tex_coord = {0.0f, 0.0f};
                        verts.push_back(sv);
                    }
                }
                if (static_cast<int>(verts.size()) >= kBatchVerts) flush();
            }
        }
    }
    flush();

    // 叠加山线条贴图（格中心、尺寸 = cellPx——格面积相等 → 山大小不变，P12 决策）。
    if (mountain_.count() > 0) {
        for (int rr = r0; rr <= r1; ++rr) {
            g.colRange(vx0, vx1, rr, c0, c1);
            for (int cc = c0; cc <= c1; ++cc) {
                const int oCount = (g.type == TilingType::Tri) ? 2 : 1;
                for (int o = 0; o < oCount; ++o) {
                    const int idx = (g.type == TilingType::Tri) ? 2 * (rr * g.cols + cc) + o
                                                                : rr * g.cols + cc;
                    const MapCell& cell = map.atIndex(idx);
                    if (!cell.land || !cell.mountain) continue;
                    double cx0, cy0;
                    g.cellCenter(idx, cx0, cy0);
                    const int size = std::max(2, static_cast<int>(std::lround(cam_.cellPx())));
                    const int ci = std::clamp(static_cast<int>(cell.belongi), 0,
                                              mountain_.count() - 1);
                    const int si = mountain_.pickSizeIndex(size);
                    SDL_Texture* tex = mountain_.texture(ci, si);
                    if (!tex) continue;
                    const int srcSize = mountain_.sizePixel(si);
                    r.drawSpriteCentered(tex, {0, 0, srcSize, srcSize},
                                         cam_.toScreenXi(cx0), cam_.toScreenYi(cy0), size);
                }
            }
        }
    }
    drawBoundaryOutline(map);
}

// P12 决策 3：任意密铺画一圈灰线描出地图边界（纯渲染；实心粗线段，无噪点）。
// 方 = 地图外框矩形；六/三 = 边界格外轮廓（邻格越界的边段）。
// 2026-08 反馈：线宽固定（不随相机缩放变化），缩放时不再出现断线/粗线糊边。
void MapRenderer::drawBoundaryOutline(const Map& map) {
    const SDL_Color kOutline = {150, 150, 150, 255};
    Renderer r(ren_);
    const TilingGeom& g = map.geom();
    const int thick = 2;  // 固定像素线宽，与相机缩放无关
    if (g.type == TilingType::Square) {
        const int x0 = cam_.toScreenXi(0.0), y0 = cam_.toScreenYi(0.0);
        const int x1 = cam_.toScreenXi(static_cast<double>(map.width()));
        const int y1 = cam_.toScreenYi(static_cast<double>(map.height()));
        r.fillThickSegment(x0, y0, x1, y0, thick, kOutline);
        r.fillThickSegment(x1, y0, x1, y1, thick, kOutline);
        r.fillThickSegment(x1, y1, x0, y1, thick, kOutline);
        r.fillThickSegment(x0, y1, x0, y0, thick, kOutline);
        return;
    }
    // 六/三：扫描可见范围，收集边界格的外露边段（neighbor 越界；cellEdge 与邻接同序）。
    const double vx0 = cam_.viewWorldX0(), vx1 = cam_.viewWorldX1();
    const double vy0 = cam_.viewWorldY0(), vy1 = cam_.viewWorldY1();
    int r0, r1, c0, c1;
    g.rowRange(vy0, vy1, r0, r1);
    for (int rr = r0; rr <= r1; ++rr) {
        g.colRange(vx0, vx1, rr, c0, c1);
        for (int cc = c0; cc <= c1; ++cc) {
            const int oCount = (g.type == TilingType::Tri) ? 2 : 1;
            for (int o = 0; o < oCount; ++o) {
                const int idx = (g.type == TilingType::Tri) ? 2 * (rr * g.cols + cc) + o
                                                            : rr * g.cols + cc;
                for (int k = 0; k < g.neighborCount(); ++k) {
                    if (g.neighbor(idx, k) >= 0) continue;  // 内边 → 跳过
                    double ex0, ey0, ex1, ey1;
                    if (!g.cellEdge(idx, k, ex0, ey0, ex1, ey1)) continue;
                    r.fillThickSegment(cam_.toScreenXi(ex0), cam_.toScreenYi(ey0),
                                       cam_.toScreenXi(ex1), cam_.toScreenYi(ey1), thick,
                                       kOutline);
                }
            }
        }
    }
}

}  // namespace lw::render
