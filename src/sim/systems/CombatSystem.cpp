#include "sim/systems/CombatSystem.h"

#include <algorithm>

#include "core/MathUtil.h"

namespace lw {

double CombatSystem::queryRadius(const Config& cfg) {
    double maxSize = 0.0;
    for (const auto& u : cfg.units) {
        maxSize = std::max(maxSize, cfg.army.baseSize * u.sizeMult);
    }
    return 2.0 * maxSize + 1.0;
}

bool CombatSystem::checkAt(MoveContext& ctx, entt::entity self, const comp::Position& pos,
                           const comp::Collider& col, const comp::FactionId& fid) {
    const double radius = queryRadius(ctx.config);
    const auto candidates = ctx.spatialHash.queryCircle(ctx.registry, pos.x, pos.y, radius);
    for (auto other : candidates) {
        if (other == self) continue;
        if (ctx.registry.all_of<comp::Dead>(other)) continue;
        if (ctx.registry.all_of<comp::Projectile>(other)) continue;  // P9：兵近战不主动打子弹
        const auto& otherPos = ctx.registry.get<comp::Position>(other);
        const auto& otherCollider = ctx.registry.get<comp::Collider>(other);
        const auto& otherFaction = ctx.registry.get<comp::FactionId>(other);
        if (otherFaction.value == fid.value) continue;  // 同势力不交战
        if (math::distance(otherPos.x, otherPos.y, pos.x, pos.y)
            < otherCollider.radius + col.radius) {
            // 双方同归于尽；P11 击杀 credit 各记对方 (faction, unitType)。
            const auto& selfUnit = ctx.registry.get<comp::UnitType>(self);
            const auto& otherUnit = ctx.registry.get<comp::UnitType>(other);
            markDead(ctx, self, pos.x, pos.y, otherPos.x, otherPos.y, otherFaction.value,
                     static_cast<int>(otherUnit.type));  // other 杀 self
            markDead(ctx, other, otherPos.x, otherPos.y, pos.x, pos.y, fid.value,
                     static_cast<int>(selfUnit.type));   // self 杀 other
            return true;
        }
    }
    return false;
}

}  // namespace lw
