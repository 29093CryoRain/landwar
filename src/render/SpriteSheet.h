// SpriteSheet.h — 军队兵表（统一 tint 管线，色相旋转方案）。
// 两张彩色源表（各 1 列 × 11 行 32×32，army_base.png 352px 高）：
//   行 0-5 = 原 6 兵种（普通/先锋/开拓/激光/爆炸/地雷）；
//   行 6-7 = 激光火花/光束（特效用，rect() 保留行号语义，见 EffectRenderer）；
//   行 8-10 = P9 新贴图（子弹/手枪/霰弹，2026-08-07 用户放入最底 3 格）。
//   data/army_base.png    — 基础版（源列0 红）。TintCache **HueRotate** 按 8 势力色烘焙：
//                           精确复现原图"色相处理"（保留明度/饱和度，白像素不动→激光白芯自动保留）。
//   data/army_special.png — 签名兵种特殊版（青先锋/蓝开拓/绿激光/橙爆炸/紫地雷），
//                           原样彩色，由所属势力直接使用（kSpecialUnitFaction 决定谁用）。
// 渲染时取势力版纹理（base）+ unitRect(type)，或特殊版纹理（special）+ unitRect(type)。
#pragma once

#include <SDL.h>

#include <array>
#include <vector>

#include "core/GameDefs.h"
#include "render/TintCache.h"

namespace lw::render {

class SpriteSheet {
public:
    SpriteSheet() = default;
    ~SpriteSheet() = default;

    // 加载 army_base.png（HueRotate → factionColors 8 份）+ army_special.png（原样）。
    bool load(SDL_Renderer* ren, const std::vector<std::array<int, 3>>& factionColors);

    // 势力色版本的基础兵表纹理（factionIndex 0..7 = 势力 id 1..8）；未加载/越界 → nullptr。
    SDL_Texture* texture(int factionIndex) const { return baseTint_.texture(factionIndex); }
    // 特殊版兵表纹理（原样彩色，单张共享）。
    SDL_Texture* specialTexture() const { return specialTint_.texture(0); }

    int spriteSize() const { return spriteSize_; }
    // 基础/特殊纹理都是单列 32×352（11 行）。
    int textureWidth() const { return baseTint_.width(); }
    int textureHeight() const { return baseTint_.height(); }

    // 显式销毁纹理（须先于 SDL_DestroyRenderer；可重复调用）。
    void release() {
        baseTint_.release();
        specialTint_.release();
    }

    // 兵种子图矩形（按兵种类型映射到行号）。行 0-5 = 原 6 兵种；
    // P9 手枪/霰弹映射到行 9/10（行 8 = 子弹）。特效用（激光火花/光束）的 rect() 见下。
    SDL_Rect unitRect(int type) const;
    // 子弹 sprite 子图（army_base.png 行 8；P9 新增贴图）。
    SDL_Rect bulletRect() const;
    // 按行号取子图（特效/激光用：行 6=火花、行 7=光束）。勿与 unitRect 混用。
    SDL_Rect rect(int row) const;

private:
    TintCache baseTint_;     // army_base.png，HueRotate → 8 势力色
    TintCache specialTint_;  // army_special.png，Multiply 恒等（白=255）→ 原样
    int spriteSize_ = 32;
};

}  // namespace lw::render
