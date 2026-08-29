// ArmyRenderer.cpp — 军队渲染实现（翻新计划 Phase 7，§2.9）。
#include "render/ArmyRenderer.h"

#include <algorithm>
#include <cmath>

#include "core/GameDefs.h"
#include "core/Simulation.h"
#include "render/RendererUtil.h"
#include "sim/components.h"

namespace lw::render {

void ArmyRenderer::draw(const Simulation& sim) {
    const auto& reg = sim.registry();
    Renderer r(ren_);
    // 兵 sprite 尺寸随缩放等比（世界等比渲染）；保留最小 2px 防退化。
    const int size = render::spriteSize(drawSize_, cam_.zoom());
    // 无 y 偏移：曾加 spriteSize/2（16px）下移以修正视觉，后取消（不加偏移视觉正常）。
    // 点选交互锚点与渲染一致（同样不加偏移），见 Application::pickSelection。
    // （2026-08 废除环绕：不再双渲染副本，仅主位置单绘制。）
    for (auto e : reg.view<comp::Position, comp::Collider, comp::FactionId, comp::UnitType,
                           comp::Velocity>()) {
        if (reg.all_of<comp::Dead>(e)) continue;
        // P9.3（2026-08-07）：子弹实体也带全兵组件（Position/Velocity/Collider/FactionId/UnitType），
        // 恰好满足本视图 → 会被当兵画出手枪/霰弹贴图，与 ProjectileRenderer 的子弹贴图重叠。
        // 显式排除 comp::Projectile：子弹只由 ProjectileRenderer 渲染（行 8 子弹贴图，不画兵贴图）。
        if (reg.all_of<comp::Projectile>(e)) continue;
        const int fid = reg.get<comp::FactionId>(e).value;
        if (fid < 1 || fid > kPlayerFactionCount) continue;  // 防御：中立/非法 id 不上场
        const int type = static_cast<int>(reg.get<comp::UnitType>(e).type);
        const auto& p = reg.get<comp::Position>(e);
        // 视野剔除（2026-08 工程改进）：实体离屏 → 跳过绘制。
        // 半径取整颗 sprite 折算格数（保守，多画无害）。
        if (!render::isVisibleOnScreen(cam_, p.x, p.y, size / cam_.cellPx())) continue;
        const int sx = cam_.toScreenXi(p.x);
        const int sy = cam_.toScreenYi(p.y);
        // 双色渲染（视觉工程改进 ⑫）：统一 army_base.png → 势力版 = 该势力
        // (主色, 副色) 合成纹理。
        SDL_Texture* tex = sheet_.texture(fid - 1);
        // P9（2026-08-07）：手枪/霰弹兵贴图随运动方向旋转——枪管（图中朝上）朝前。
        // 子弹与其他兵不旋转（drawSpriteCentered）。公式：朝上贴图 = 激光朝下贴图公式 +180°
        //（激光角 = -angle-π/2，见 EffectRenderer；up 贴图反向即 +π）。
        const bool rotated =
            (type == static_cast<int>(ArmyType::pistol) || type == static_cast<int>(ArmyType::shotgun));
        const double angleDeg = rotated
                                    ? (-reg.get<comp::Velocity>(e).angle + kPi / 2.0) * 180.0 / kPi
                                    : 0.0;
        if (rotated) {
            r.drawSpriteRotated(tex, sheet_.unitRect(type), sx - size / 2, sy - size / 2,
                                size, size, angleDeg, size / 2, size / 2);
        } else {
            r.drawSpriteCentered(tex, sheet_.unitRect(type), sx, sy, size);
        }
    }
}

}  // namespace lw::render
