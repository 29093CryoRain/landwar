// SpriteSheet.cpp — 兵表实现（色相旋转 tint）。
#include "render/SpriteSheet.h"

#include <spdlog/spdlog.h>

namespace lw::render {

bool SpriteSheet::load(SDL_Renderer* ren, const std::vector<std::array<int, 3>>& factionColors) {
    // 基础版：彩色源（红）→ HueRotate 到各势力色（精确复现原图色相处理）。
    const bool baseOk =
        baseTint_.load(ren, "data/army_base.png", factionColors, TintMode::HueRotate);
    // 特殊版：原样彩色（Multiply 恒等，白=255 → 不改变）。
    const std::vector<std::array<int, 3>> identity{{255, 255, 255}};
    const bool specialOk =
        specialTint_.load(ren, "data/army_special.png", identity, TintMode::Multiply);
    if (!baseOk || !specialOk) {
        spdlog::error("SpriteSheet: sheet load incomplete (base={} special={})", baseOk,
                      specialOk);
        return false;
    }
    return true;
}

SDL_Rect SpriteSheet::unitRect(int type) const {
    // 兵种 → 行号映射。行 0-5 = 原 6 兵种；行 6/7 为激光火花/光束（特效用，rect() 保留）；
    // 行 8 = 子弹；行 9 = 手枪；行 10 = 霰弹（P9 用户新增贴图，army_base.png 最底 3 格）。
    static constexpr int kUnitRow[kArmyTypeCount] = {0, 1, 2, 3, 4, 5, 9, 10};
    const int row = (type >= 0 && type < kArmyTypeCount) ? kUnitRow[type] : type;
    return SDL_Rect{0, row * spriteSize_, spriteSize_, spriteSize_};
}

SDL_Rect SpriteSheet::bulletRect() const {
    return SDL_Rect{0, 8 * spriteSize_, spriteSize_, spriteSize_};
}

SDL_Rect SpriteSheet::rect(int row) const {
    return SDL_Rect{0, row * spriteSize_, spriteSize_, spriteSize_};
}

}  // namespace lw::render
