#include "sim/systems/SpawnSystem.h"

#include <algorithm>
#include <iterator>

#include "core/MathUtil.h"
#include "core/Simulation.h"

namespace lw {

entt::entity SpawnSystem::spawnArmy(Simulation& sim, double x, double y, int factionId,
                                    ArmyType type) {
    auto& reg = sim.registry();
    const auto& cfg = sim.config();
    // maxArmyGuard 仅防跑飞保险（原版固定池满返回哨兵；新版无硬上限）。
    const auto view = reg.view<comp::Position, comp::Collider>();
    if (static_cast<int>(std::distance(view.begin(), view.end())) >= cfg.sim.maxArmyGuard) {
        return entt::null;
    }

    entt::entity e = sim.createEntity();  // 单调 id（快照读档后 id 分配确定）

    // 出生方向：0~2π 纯随机（Phase 9 修正，对齐思路.txt「随机方向」，去掉原轴向吸附）。
    const double angle = math::getRandomAngle(sim.rng());

    // 速度链（§2.2）：base × 势力速度增益（P7 改读 mods，含原 speedMultAll/pioneerSpeedMult）
    // × 类型乘数。基线 mods.speedMult=1.0 → ×1.0 恒等，行为不变。
    double speed = cfg.army.baseSpeed;
    speed *= sim.faction(factionId).mods.speedMult[static_cast<size_t>(type)];
    speed *= cfg.units[static_cast<int>(type)].speedMult;

    const double size = cfg.army.baseSize * cfg.units[static_cast<int>(type)].sizeMult;

    // 出生在山格（P5：山城等）→ 直接处于山地，速度按山地系数缩放（与 moveArmy 语义自洽）。
    // 细节改进：开拓兵在山地不减速 → 出生在山也不缩放。不消耗 RNG；现有地图无山 → 基线行为不变。
    const int gx = static_cast<int>(x), gy = static_cast<int>(y);
    const bool spawnInMountain = (gx >= 0 && gx < sim.map().width() && gy >= 0
                                  && gy < sim.map().height())
                                 && sim.map().at(gx, gy).mountain;
    if (spawnInMountain && slowsInMountain(cfg, static_cast<int>(type)))
        speed *= cfg.terrain.mountainSpeedMult;

    reg.emplace<comp::Position>(e, x, y);
    reg.emplace<comp::Velocity>(e, angle);
    reg.emplace<comp::Speed>(e, speed);
    reg.emplace<comp::OnLand>(e, true);
    reg.emplace<comp::MountainState>(e, spawnInMountain);
    reg.emplace<comp::FactionId>(e, factionId);
    reg.emplace<comp::UnitType>(e, type);
    reg.emplace<comp::Collider>(e, size);
    reg.emplace<comp::LandHistory>(e, static_cast<int>(sim.tickCount()));
    // P9 行为抽象：从兵种定义填入 Behavior（死亡特效 + 周期动作，与迁移前映射一致）。
    const auto& udef = cfg.units[static_cast<size_t>(static_cast<int>(type))];
    reg.emplace<comp::Behavior>(e, comp::Behavior{udef.deathEffect, udef.periodic,
                                                  udef.periodTicks, 0});
    // P11：产兵统计（含势力8 免费兵——spawnArmy 是唯一产兵路径）。
    sim.stats().recordProduced(factionId, static_cast<int>(type));
    return e;
}

entt::entity SpawnSystem::spawnProjectile(Simulation& sim, double x, double y, int factionId,
                                          ArmyType sourceType, double angle, double speed) {
    auto& reg = sim.registry();
    const auto& cfg = sim.config();
    entt::entity e = sim.createEntity();  // 单调 id

    const auto& udef = cfg.units[static_cast<size_t>(static_cast<int>(sourceType))];
    // 子弹出生在山格（发射兵在山内开火）→ inMountain=true（山地留存惩罚；陆→山仍会消失）。
    // P12：密铺按格下标判定山（方 floor 坐标在六/三角下取错格）。
    const Map& map = sim.map();
    int inMountainCell = -1;
    if (map.tiling() == TilingType::Square) {
        const int gx = static_cast<int>(x), gy = static_cast<int>(y);
        if (gx >= 0 && gx < map.width() && gy >= 0 && gy < map.height())
            inMountainCell = gy * map.width() + gx;
    } else {
        inMountainCell = map.geom().worldToCell(x, y);
    }
    const bool inMountain = inMountainCell >= 0 && map.atIndex(inMountainCell).mountain;

    reg.emplace<comp::Position>(e, x, y);
    reg.emplace<comp::Velocity>(e, angle);
    reg.emplace<comp::Speed>(e, speed);
    reg.emplace<comp::FactionId>(e, factionId);
    reg.emplace<comp::UnitType>(e, sourceType);
    reg.emplace<comp::Collider>(e, udef.bulletSize);
    reg.emplace<comp::Projectile>(e, comp::Projectile{udef.bulletLifespanTicks, inMountain});
    return e;
}

entt::entity SpawnSystem::spawnEffect(Simulation& sim, double x, double y, int factionId,
                                      EffectType type, double p0, const comp::Creator& creator) {
    auto& reg = sim.registry();
    const auto& map = sim.map();
    entt::entity e = sim.createEntity();  // 单调 id
    // 原版 add_effect：x,y 夹取到 [0, Map_x]×[0, Map_y]（P12：六/三角用世界范围）。
    const double ex = (sim.map().tiling() == TilingType::Square)
                          ? static_cast<double>(map.width())
                          : map.worldWidth();
    const double ey = (sim.map().tiling() == TilingType::Square)
                          ? static_cast<double>(map.height())
                          : map.worldHeight();
    reg.emplace<comp::Position>(e, std::clamp(x, 0.0, ex), std::clamp(y, 0.0, ey));
    reg.emplace<comp::FactionId>(e, factionId);
    reg.emplace<comp::EffectTypeId>(e, type);
    reg.emplace<comp::EffectTimer>(e, static_cast<int>(sim.tickCount()));
    reg.emplace<comp::EffectParams>(e, p0, 0.0, 0.0);
    reg.emplace<comp::Creator>(e, creator);
    return e;
}

}  // namespace lw
