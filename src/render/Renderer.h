// Renderer.h — SDL_Renderer 封装（翻新计划 Phase 7，§3.1 render/）。
// 提供颜色转换（势力色 array<int,3> / 0x00RRGGBB ↔ SDL_Color）、矩形/圆填充、
// 带 alpha 与旋转的纹理绘制。渲染层约定：所有坐标已是屏幕像素
// （MapRenderer/ArmyRenderer/EffectRenderer 用 math::ScreenTransform 换算）。
#pragma once

#include <SDL.h>

#include <array>
#include <vector>

namespace lw::render {

// SDL_Renderer 薄封装。非拥有 SDL_Renderer（生命周期归 Application）。
class Renderer {
public:
    explicit Renderer(SDL_Renderer* ren) : ren_(ren) {}

    SDL_Renderer* raw() const { return ren_; }

    // ---- 颜色工具 ----
    static SDL_Color toColor(int r, int g, int b, int a = 255);
    static SDL_Color toColor(const std::array<int, 3>& rgb, int a = 255);
    // 0x00RRGGBB ↔ SDL_Color（原版 RGB_COLOR 去最高位）。
    static SDL_Color unpack(unsigned packed, int a = 255);
    static unsigned pack(int r, int g, int b);
    // 配合 math::mixColor：pack(势力色) → mixColor → unpack（地图格配色用）。
    static SDL_Color mixed(const std::array<int, 3>& rgb, unsigned other, double rate, int a = 255);

    // ---- 几何 ----
    // 实心矩形（alpha<255 时生效；渲染器 blend 已设为 BLEND）。
    void fillRect(int x, int y, int w, int h, const SDL_Color& c);
    // 批量实心矩形（同色一组）：单次 SDL_RenderFillRects 调用画一组，
    // 取代逐格 fillRect（地图 ~6000 格 → ~16 组，避免 CPU 驱动调用开销烧满预算，见卡顿修复）。
    void fillRects(const std::vector<SDL_Rect>& rects, const SDL_Color& c);
    // 实心圆（扫描线填充，等价 DxLib DrawCircle FillFlag=TRUE）。radius<=0 时跳过。
    void fillCircle(int cx, int cy, int radius, const SDL_Color& c);

    // ---- 纹理 ----
    // 以中心 (cx,cy) 绘制 src 子图，拉伸到 size×size（原版 DrawExtendGraph 行为）。
    // alpha 经 SDL_SetTextureAlphaMod 施加（地雷闪烁用）。
    void drawSpriteCentered(SDL_Texture* tex, const SDL_Rect& src, int cx, int cy, int size,
                            int alpha = 255);
    // 非方形变体：以中心 (cx,cy) 绘制 src 子图，拉伸到 dstW×dstH（保留纵横比由调用方算好）。
    // P13 城市等级贴图（data/tower/tower<N>.png，高塔非正方形）用。
    void drawSpriteCenteredRect(SDL_Texture* tex, const SDL_Rect& src, int cx, int cy, int dstW,
                                int dstH, int alpha = 255);
    // SDL_RenderCopyEx 等价（DxLib DrawRotaGraph3）：dst 矩形 + 旋转角(deg) + pivot。
    // center 相对 dst 左上（激光光束：dst 宽16 高15*len，pivot=(8,0)=顶中点）。
    void drawSpriteRotated(SDL_Texture* tex, const SDL_Rect& src, int x, int y, int w, int h,
                           double angleDeg, int centerX, int centerY, int alpha = 255);

private:
    SDL_Renderer* ren_ = nullptr;
};

}  // namespace lw::render
