#include "sim/systems/DeathSystem.h"

#include <vector>

#include "core/MathUtil.h"
#include "core/Simulation.h"
#include "sim/systems/SpawnSystem.h"

namespace lw {

namespace {

// 死亡效果（§2.5）：不区分死因，按死者行为/势力生成。
// 特效的创建者快照 = 死者位置/势力/兵种（原版 creator_id 语义，死兵位置不随 tick 变化）。
// P11：Creator.unitType 记录死者兵种 → 特效击杀/占领 credit 链回该 (faction, unitType)。
void spawnDeathEffects(Simulation& sim, const DeathEvent& d) {
    const auto& cfg = sim.config();
    const comp::Creator creator{d.x, d.y, d.factionId, static_cast<int>(d.type)};
    // P9 行为抽象：死亡特效来自 comp::Behavior（spawnArmy 由 config 填入）。
    // try_get 防手动建实体（无 Behavior）——回退 config 定义，行为与迁移前硬编码映射一致。
    const auto* bh = sim.registry().try_get<comp::Behavior>(d.entity);
    const DeathEffect de = bh ? bh->deathEffect
                              : cfg.units[static_cast<size_t>(static_cast<int>(d.type))].deathEffect;
    switch (de) {
        case DeathEffect::laser: {
            // 激光：向击杀者方向；额外条数（P7 改读 mods.laserExtraBeams，绿5=2 → ±π/11 各一条）。
            // 生成顺序保持迁移前：side 交替（-spread,+spread...）→ 主束（实体 id/RNG 序列不变）。
            const double angle = math::getAngle(d.x, d.y, d.killerX, d.killerY);
            const auto& mods = sim.faction(d.factionId).mods;
            if (mods.laserExtraBeams > 0) {
                const double spread = kPi * cfg.effect.laser.beamSpreadPIFrac;
                for (int i = 0; i < mods.laserExtraBeams; ++i) {
                    const double s = (i % 2 == 0) ? -spread : spread;
                    SpawnSystem::spawnEffect(sim, d.x, d.y, d.factionId, EffectType::laser,
                                             angle + s, creator);
                }
            }
            SpawnSystem::spawnEffect(sim, d.x, d.y, d.factionId, EffectType::laser, angle, creator);
            break;
        }
        case DeathEffect::bomb: {
            // 爆炸：基础半径 ×（1 + 橙色势力加成）× 科技爆炸半径乘数。
            const double radius =
                cfg.effect.bomb.baseRadius
                * (1.0 + (d.factionId == 6 ? cfg.factions[6].bombRadiusBonus : 0.0))
                * sim.faction(d.factionId).mods.bombRadiusMult;
            SpawnSystem::spawnEffect(sim, d.x, d.y, d.factionId, EffectType::bomb, radius, creator);
            break;
        }
        case DeathEffect::mine:
            SpawnSystem::spawnEffect(sim, d.x, d.y, d.factionId, EffectType::mine, 0.0, creator);
            break;
        case DeathEffect::none:
            break;
    }
}

}  // namespace

void DeathSystem::flush(Simulation& sim) {
    auto& reg = sim.registry();
    std::vector<DeathEvent> deaths = sim.takeDeaths();
    for (const auto& d : deaths) {
        if (!reg.valid(d.entity)) continue;
        spawnDeathEffects(sim, d);
        reg.destroy(d.entity);
    }
}

}  // namespace lw
