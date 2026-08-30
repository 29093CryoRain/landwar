// SpriteSheet.h — 军队兵表（双色渲染管线，视觉工程改进 ⑫）。
// 单张彩色源表（1 列 × 11 行 32×32，army_base.png 352px 高）：
//   行 0-5 = 原 6 兵种（普通/先锋/开拓/激光/爆炸/地雷）；
//   行 6-7 = 激光火花/光束（特效用，rect() 保留行号语义，见 EffectRenderer）；
//   行 8-10 = P9 新贴图（子弹/手枪/霰弹，2026-08-07 用户放入最底 3 格）。
// 渲染：把源图视为"红势力（主红/副浅灰 191）的成品渲染"→ 逐像素反解 主/副/黑白比例+色相
// 偏移 模板 → 按各势力 (主色, 副色) 重合成（TwoToneTintCache）。性质：
//   - 副色=浅灰191 时与旧 HueRotate 逐像素等价 → 默认调色板观感不变；
//   - 红势力还原源图；白芯（激光/子弹）天然保留；色相偏移照旧平移。
// 运行时统一加载 army_base.png。
// 渲染时取势力版纹理（base）+ unitRect(type)。
#pragma once

#include <SDL.h>

#include <array>
#include <utility>
#include <vector>

#include "core/GameDefs.h"
#include "render/TwoToneTintCache.h"

namespace lw::render {

class SpriteSheet {
public:
    SpriteSheet() = default;
    ~SpriteSheet() = default;

    // 加载 army_base.png，按 palettes（下标 = 势力 id-1，各 (主色, 副色)）烘焙 8 份双色纹理。
    bool load(SDL_Renderer* ren,
              const std::vector<std::pair<std::array<int, 3>, std::array<int, 3>>>& palettes);

    // 势力版双色兵表纹理（factionIndex 0..7 = 势力 id 1..8）；未加载/越界 → nullptr。
    SDL_Texture* texture(int factionIndex) const { return baseTint_.texture(factionIndex); }

    // 双色纹理都是单列 32×352（11 行）。
    int textureWidth() const { return baseTint_.width(); }
    int textureHeight() const { return baseTint_.height(); }

    // 显式销毁纹理（须先于 SDL_DestroyRenderer；可重复调用）。
    void release() { baseTint_.release(); }

    // 兵种子图矩形（按兵种类型映射到行号）。行 0-5 = 原 6 兵种；
    // P9 手枪/霰弹映射到行 9/10（行 8 = 子弹）。特效用（激光火花/光束）的 rect() 见下。
    SDL_Rect unitRect(int type) const;
    // 子弹 sprite 子图（army_base.png 行 8；P9 新增贴图）。
    SDL_Rect bulletRect() const;
    // 按行号取子图（特效/激光用：行 6=火花、行 7=光束）。勿与 unitRect 混用。
    SDL_Rect rect(int row) const;

private:
    TwoToneTintCache baseTint_;  // army_base.png → 8 势力双色合成
    static constexpr int kSourceSpriteSize = 32;  // army_base.png 每格源图尺寸（源图像素）
};

}  // namespace lw::render
