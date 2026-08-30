// SpriteSheet.cpp — 兵表实现（双色渲染管线，视觉工程改进 ⑫）。
#include "render/SpriteSheet.h"

#include <spdlog/spdlog.h>

namespace lw::render {

bool SpriteSheet::load(
    SDL_Renderer* ren,
    const std::vector<std::pair<std::array<int, 3>, std::array<int, 3>>>& palettes) {
    // 基础版：源图 = 红势力成品渲染 → 反解模板 → 按 (主色, 副色) 对重合成（TwoToneTintCache）。
    // 统一使用基础兵表。
    if (!baseTint_.load(ren, "data/army_base.png", palettes)) {
        spdlog::error("SpriteSheet: sheet load failed");
        return false;
    }
    return true;
}

SDL_Rect SpriteSheet::unitRect(int type) const {
    // 兵种 → 行号映射。行 0-5 = 原 6 兵种；行 6/7 为激光火花/光束（特效用，rect() 保留）；
    // 行 8 = 子弹；行 9 = 手枪；行 10 = 霰弹（P9 用户新增贴图，army_base.png 最底 3 格）。
    static constexpr int kUnitRow[kArmyTypeCount] = {0, 1, 2, 3, 4, 5, 9, 10};
    const int row = (type >= 0 && type < kArmyTypeCount) ? kUnitRow[type] : type;
    return SDL_Rect{0, row * kSourceSpriteSize, kSourceSpriteSize, kSourceSpriteSize};
}

SDL_Rect SpriteSheet::bulletRect() const {
    return SDL_Rect{0, 8 * kSourceSpriteSize, kSourceSpriteSize, kSourceSpriteSize};
}

SDL_Rect SpriteSheet::rect(int row) const {
    return SDL_Rect{0, row * kSourceSpriteSize, kSourceSpriteSize, kSourceSpriteSize};
}

}  // namespace lw::render
