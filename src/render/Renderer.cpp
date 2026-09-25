// Renderer.cpp — SDL_Renderer 封装实现（翻新计划 Phase 7）。
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>

#include "core/MathUtil.h"

namespace lw::render {

SDL_Color Renderer::toColor(int r, int g, int b, int a) {
    return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b),
                     static_cast<Uint8>(a)};
}

SDL_Color Renderer::toColor(const std::array<int, 3>& rgb, int a) {
    return toColor(rgb[0], rgb[1], rgb[2], a);
}

SDL_Color Renderer::unpack(unsigned packed, int a) {
    return toColor(static_cast<int>((packed >> 16) & 0xFF), static_cast<int>((packed >> 8) & 0xFF),
                   static_cast<int>(packed & 0xFF), a);
}

unsigned Renderer::pack(int r, int g, int b) {
    return (static_cast<unsigned>(r) << 16) | (static_cast<unsigned>(g) << 8)
           | static_cast<unsigned>(b);
}

SDL_Color Renderer::mixed(const std::array<int, 3>& rgb, unsigned other, double rate, int a) {
    return unpack(math::mixColor(pack(rgb[0], rgb[1], rgb[2]), other, rate), a);
}

void Renderer::fillRect(int x, int y, int w, int h, const SDL_Color& c) {
    if (w <= 0 || h <= 0) return;
    SDL_Rect r{x, y, w, h};
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren_, &r);
}

void Renderer::fillRects(const std::vector<SDL_Rect>& rects, const SDL_Color& c) {
    if (rects.empty()) return;
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_RenderFillRects(ren_, rects.data(), static_cast<int>(rects.size()));
}

void Renderer::fillCircle(int cx, int cy, int radius, const SDL_Color& c) {
    if (radius <= 0) return;
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    const int r2 = radius * radius;
    // 逐行水平扫描线（实心圆；DxLib DrawCircle FillFlag=TRUE 等价）。
    for (int dy = -radius; dy <= radius; ++dy) {
        const int hw = static_cast<int>(std::sqrt(std::max(0, r2 - dy * dy)));
        SDL_RenderDrawLine(ren_, cx - hw, cy + dy, cx + hw, cy + dy);
    }
}

// 粗线段：沿线段法向外扩 thick/2 得四角 → 三角化填充（实心、无接缝）。
void Renderer::fillThickSegment(int x0, int y0, int x1, int y1, int thick, const SDL_Color& c) {
    if (thick <= 0) return;
    const double dx = static_cast<double>(x1 - x0), dy = static_cast<double>(y1 - y0);
    const double len = std::hypot(dx, dy);
    const double hw = thick * 0.5;
    double nx = 0.0, ny = 0.0;
    if (len > 1e-9) {
        nx = -dy / len * hw;
        ny = dx / len * hw;
    }
    // 四角（外扩后的矩形）。
    const float ax = static_cast<float>(x0 + nx), ay = static_cast<float>(y0 + ny);
    const float bx = static_cast<float>(x1 + nx), by = static_cast<float>(y1 + ny);
    const float cx2 = static_cast<float>(x1 - nx), cy2 = static_cast<float>(y1 - ny);
    const float dx2 = static_cast<float>(x0 - nx), dy2 = static_cast<float>(y0 - ny);
    SDL_Vertex verts[6] = {
        {{ax, ay}, c, {0.0f, 0.0f}}, {{bx, by}, c, {0.0f, 0.0f}}, {{cx2, cy2}, c, {0.0f, 0.0f}},
        {{ax, ay}, c, {0.0f, 0.0f}}, {{cx2, cy2}, c, {0.0f, 0.0f}}, {{dx2, dy2}, c, {0.0f, 0.0f}},
    };
    SDL_RenderGeometry(ren_, nullptr, verts, 6, nullptr, 0);
}

// 亚像素粗线段：端点/线宽全为 double。**不把端点沿方向外扩**：折线顶点处的圆角/端帽由调用方
// 补 `fillDiscF`（半径恰为 thick/2）完成，两者之并 = 折线与圆盘的 Minkowski 和（严格等宽圆角）；
// 若再沿方向外扩半宽，拐角外侧会多出 hw·(√2−1) 的**方块凸起**（"拐角瑕疵"，回归见
// test_river_render 的 CornerJoinStaysWithinHalfWidth）。
void Renderer::fillThickSegmentF(double x0, double y0, double x1, double y1, double thick,
                                const SDL_Color& c) {
    if (thick <= 0.0) return;
    const double dx = x1 - x0, dy = y1 - y0;
    const double len = std::hypot(dx, dy);
    if (len <= 1e-12) return;
    const double hw = thick * 0.5;
    const double nx = -dy / len * hw, ny = dx / len * hw;  // 单位法向 × 半宽
    const float ax = static_cast<float>(x0 + nx), ay = static_cast<float>(y0 + ny);
    const float bx = static_cast<float>(x1 + nx), by = static_cast<float>(y1 + ny);
    const float cx2 = static_cast<float>(x1 - nx), cy2 = static_cast<float>(y1 - ny);
    const float dx2 = static_cast<float>(x0 - nx), dy2 = static_cast<float>(y0 - ny);
    SDL_Vertex verts[6] = {
        {{ax, ay}, c, {0.0f, 0.0f}}, {{bx, by}, c, {0.0f, 0.0f}}, {{cx2, cy2}, c, {0.0f, 0.0f}},
        {{ax, ay}, c, {0.0f, 0.0f}}, {{cx2, cy2}, c, {0.0f, 0.0f}}, {{dx2, dy2}, c, {0.0f, 0.0f}},
    };
    SDL_RenderGeometry(ren_, nullptr, verts, 6, nullptr, 0);
}

// 亚像素实心圆：正 n 边形三角扇形（半径即真实半径）。小半径（<=1.5px）用 8 边形已看不出棱角。
void Renderer::fillDiscF(double cx, double cy, double radius, const SDL_Color& c, int segments) {
    if (radius <= 0.0) return;
    if (segments <= 0) segments = radius <= 1.5 ? 8 : (radius <= 4.0 ? 16 : 24);
    if (segments < 3) segments = 3;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<SDL_Vertex> verts;
    verts.reserve(static_cast<std::size_t>(segments) * 3);
    const float fx = static_cast<float>(cx), fy = static_cast<float>(cy);
    for (int i = 0; i < segments; ++i) {
        const double a0 = 2.0 * kPi * i / segments;
        const double a1 = 2.0 * kPi * (i + 1) / segments;
        verts.push_back({{fx, fy}, c, {0.0f, 0.0f}});
        verts.push_back({{static_cast<float>(cx + radius * std::cos(a0)),
                          static_cast<float>(cy + radius * std::sin(a0))},
                         c, {0.0f, 0.0f}});
        verts.push_back({{static_cast<float>(cx + radius * std::cos(a1)),
                          static_cast<float>(cy + radius * std::sin(a1))},
                         c, {0.0f, 0.0f}});
    }
    SDL_RenderGeometry(ren_, nullptr, verts.data(), static_cast<int>(verts.size()), nullptr, 0);
}

void Renderer::drawSpriteCentered(SDL_Texture* tex, const SDL_Rect& src, int cx, int cy, int size,
                                  int alpha) {
    if (!tex) return;
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alpha));
    const SDL_Rect dst{cx - size / 2, cy - size / 2, size, size};
    SDL_RenderCopy(ren_, tex, &src, &dst);
    SDL_SetTextureAlphaMod(tex, 255);  // 复位，避免影响后续同纹理绘制
}

void Renderer::drawSpriteCenteredRect(SDL_Texture* tex, const SDL_Rect& src, int cx, int cy,
                                       int dstW, int dstH, int alpha) {
    if (!tex) return;
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alpha));
    const SDL_Rect dst{cx - dstW / 2, cy - dstH / 2, dstW, dstH};
    SDL_RenderCopy(ren_, tex, &src, &dst);
    SDL_SetTextureAlphaMod(tex, 255);  // 复位，避免影响后续同纹理绘制
}

void Renderer::drawSpriteRotated(SDL_Texture* tex, const SDL_Rect& src, int x, int y, int w, int h,
                                 double angleDeg, int centerX, int centerY, int alpha) {
    if (!tex) return;
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alpha));
    const SDL_Rect dst{x, y, w, h};
    const SDL_Point center{centerX, centerY};
    SDL_RenderCopyEx(ren_, tex, &src, &dst, angleDeg, &center, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(tex, 255);
}

}  // namespace lw::render
