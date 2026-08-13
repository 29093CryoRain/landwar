// ProjectileRenderer.h — 子弹渲染（开发计划 P9 任务 5）。
// 子弹画 army_base.png 底部子弹 sprite（行 8，势力色 tint 管线），边长 config.projectile.drawSize，随相机缩放。
#pragma once

#include <SDL.h>

#include "render/Camera.h"
#include "render/Renderer.h"

namespace lw {

class Simulation;  // 前向声明

namespace render {

class SpriteSheet;  // 前向声明

class ProjectileRenderer {
public:
    ProjectileRenderer(SDL_Renderer* ren, const SpriteSheet& sheet, const Camera& cam,
                       int drawSize);

    // 绘制全部子弹（comp::Projectile 实体）。
    void draw(const Simulation& sim);

private:
    SDL_Renderer* ren_;
    const SpriteSheet& sheet_;
    const Camera& cam_;
    int drawSize_;  // 子弹 sprite 边长（逻辑像素，zoom=1）
};

}  // namespace lw::render
}  // namespace lw
