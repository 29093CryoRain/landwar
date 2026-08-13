// UnitActionSystem.cpp — 兵周期动作（开发计划 P9 任务 0）。
// 手枪：向兵当前朝向（Velocity.angle）发 1 颗恒定速度子弹（0 RNG）。
// 霰弹：向正前方 bulletSpreadPIFrac·π 扇形均布发 bulletCount 颗随机速度子弹
//      （每颗 2 次 RNG：角度抖动、速度，顺序固定）。
// 确定性：视图按 Behavior 存储序迭代（与实体创建序一致）；发射顺序固定 → RNG 消耗位置确定。
#include "sim/systems/UnitActionSystem.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/MathUtil.h"
#include "core/Simulation.h"
#include "sim/systems/MovementSystem.h"
#include "sim/systems/SpawnSystem.h"
#include "sim/systems/Systems.h"

namespace lw {

namespace {

// 触发一次发射（P9）。角度/速度语义见 Config::Unit::bullet*。
void fire(Simulation& sim, MoveContext& ctx, entt::entity e, PeriodicAction action) {
    auto& reg = ctx.registry;
    const auto& pos = reg.get<comp::Position>(e);
    const auto& fid = reg.get<comp::FactionId>(e);
    const auto& ut = reg.get<comp::UnitType>(e);
    const auto& udef = ctx.config.units[static_cast<size_t>(static_cast<int>(ut.type))];
    // P8 弹幕：额外子弹数（mods.projectileCountExtra[兵种]；基线 0 → 行为不变）。
    const int extra =
        ctx.factions[static_cast<size_t>(fid.value)].mods.projectileCountExtra[static_cast<size_t>(
            static_cast<int>(ut.type))];
    switch (action) {
        case PeriodicAction::firePistol: {
            // 向正前方：兵当前朝向（0 RNG；反弹会改朝向，语义自洽）。
            const double angle = reg.get<comp::Velocity>(e).angle;
            SpawnSystem::spawnProjectile(sim, pos.x, pos.y, fid.value, ut.type, angle,
                                         udef.bulletSpeed);
            break;
        }
        case PeriodicAction::fireShotgun: {
            // 正前方扇形散射（2026-08-07）：均布占位 -half..+half 防全偏一侧，再叠加轻抖动。
            // 半角 half = π·bulletSpreadPIFrac/2（霰弹全角 40° → half=π/9=20°）。
            // 抖动 ≤ 半角（frac≤1）→ 最外两颗必分居朝向两侧，不可能全偏一侧（轻度规避堆叠）。
            // 每颗 2 次 RNG：角度抖动 rng.range(-jitter,jitter) 后速度 rng.range(...)（顺序固定）。
            const double center = reg.get<comp::Velocity>(e).angle;
            const double half = kPi * udef.bulletSpreadPIFrac / 2.0;
            const int n = udef.bulletCount + extra;  // P8 弹幕：额外子弹
            for (int i = 0; i < n; ++i) {
                double nominal = center;
                if (n > 1) nominal = center - half + (2.0 * half) * i / (n - 1);
                const double jitter = udef.bulletSpreadJitterFrac * half;
                const double angle = nominal + ctx.rng.range(-jitter, jitter);
                const double speed = ctx.rng.range(udef.bulletSpeed - udef.bulletSpeedJitter,
                                                   udef.bulletSpeed + udef.bulletSpeedJitter);
                SpawnSystem::spawnProjectile(sim, pos.x, pos.y, fid.value, ut.type, angle, speed);
            }
            break;
        }
        case PeriodicAction::none:
            break;
    }
}

}  // namespace

void UnitActionSystem::update(Simulation& sim) {
    MoveContext ctx = MovementSystem::makeContext(sim);
    auto& reg = ctx.registry;
    // 快照实体列表：fire 会 create 新实体（子弹），防遍历中修改 Behavior 存储。
    // P9 确定性：按实体 id 降序迭代（与存储序无关 → 读档后 RNG 与直跑一致）。
    auto view = reg.view<comp::Behavior, comp::Velocity, comp::Position, comp::FactionId,
                         comp::UnitType>();
    std::vector<entt::entity> units(view.begin(), view.end());
    std::sort(units.begin(), units.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) > entt::to_integral(b);
    });
    for (auto e : units) {
        if (!reg.valid(e)) continue;
        if (reg.all_of<comp::Dead>(e)) continue;  // 本 tick 已被战斗标死
        auto& bh = reg.get<comp::Behavior>(e);
        if (bh.periodic == PeriodicAction::none || bh.periodTicks <= 0) continue;
        // P8 高速射击：射速周期 = round(basePeriod / mods.actionRateMult[兵种])（基线 1.0 → 恒等）。
        const auto& fid = reg.get<comp::FactionId>(e);
        const auto& ut = reg.get<comp::UnitType>(e);
        const double rate = sim.faction(fid.value).mods.actionRateMult[static_cast<size_t>(
            static_cast<int>(ut.type))];
        const int period =
            std::max(1, static_cast<int>(std::llround(static_cast<double>(bh.periodTicks) / rate)));
        if (++bh.counter < period) continue;
        bh.counter = 0;
        fire(sim, ctx, e, bh.periodic);
    }
}

}  // namespace lw
