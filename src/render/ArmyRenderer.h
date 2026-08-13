// ArmyRenderer.h — 军队渲染（翻新计划 Phase 7，§3.1 render/）。
// 每兵以 (toscreenx, toscreeny) 为中心 48×48 拉伸绘制 army.png 对应子图，
// 启用 alpha（BLEND），不翻转。子图 = rect(type, faction-1)（行=type、列=faction）。
#pragma once

#include <SDL.h>

#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/SpriteSheet.h"

namespace lw {

class Simulation;  // 前向声明（避免引入 core/Simulation.h 重依赖）

namespace render {

class ArmyRenderer {
public:
    ArmyRenderer(SDL_Renderer* ren, const SpriteSheet& sheet, const Camera& cam, int drawSize = 48)
        : ren_(ren), sheet_(sheet), cam_(cam), drawSize_(drawSize) {}

    // 绘制全部非 Dead 活兵。跳过 Dead（tick 末才销毁，渲染时可能仍在 registry）。
    // wrap=true（P10 边界贯通）：距图边 < 绘制尺寸的兵在对侧画副本（含角上对角）；关闭时不画。
    void draw(const Simulation& sim, bool wrap);

    int drawSize() const { return drawSize_; }

private:
    SDL_Renderer* ren_;
    const SpriteSheet& sheet_;
    const Camera& cam_;
    int drawSize_;  // 原版 DrawExtendGraph -24..+24（config.render.armyDrawSize）
};

}  // namespace lw::render
}  // namespace lw
