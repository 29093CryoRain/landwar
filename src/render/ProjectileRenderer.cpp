// ProjectileRenderer.cpp — 子弹渲染（开发计划 P9 任务 5）。
#include "render/ProjectileRenderer.h"

#include <algorithm>
#include <cmath>

#include "core/GameDefs.h"
#include "core/Simulation.h"
#include "render/RendererUtil.h"
#include "render/SpriteSheet.h"
#include "sim/components.h"

namespace lw::render {

ProjectileRenderer::ProjectileRenderer(SDL_Renderer* ren, const SpriteSheet& sheet,
                                       const Camera& cam, int drawSize)
    : ren_(ren), sheet_(sheet), cam_(cam), drawSize_(drawSize) {}

void ProjectileRenderer::draw(const Simulation& sim) {
    const auto& reg = sim.registry();
    Renderer r(ren_);
    const double z = cam_.zoom();
    for (auto e : reg.view<comp::Projectile, comp::Position, comp::FactionId>()) {
        const int fid = reg.get<comp::FactionId>(e).value;
        if (fid < 1 || fid > kPlayerFactionCount) continue;
        const auto& pos = reg.get<comp::Position>(e);
        // 子弹 sprite（army_base.png 行 8，势力色 tint）：边长 drawSize×zoom（最小 2px 防退化）。
        const int s = render::spriteSize(drawSize_, z);
        // 视野剔除（2026-08 工程改进）：半径取整颗 sprite 折算格数（保守）。
        if (!render::isVisibleOnScreen(cam_, pos.x, pos.y, s / cam_.cellPx())) continue;
        r.drawSpriteCentered(sheet_.texture(fid - 1), sheet_.bulletRect(),
                             cam_.toScreenXi(pos.x), cam_.toScreenYi(pos.y), s);
    }
}

}  // namespace lw::render
