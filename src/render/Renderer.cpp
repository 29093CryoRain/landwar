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
