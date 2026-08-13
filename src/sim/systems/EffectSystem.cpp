// EffectSystem.cpp — 特效求解（翻新计划 §2.6，原版 Effect.cpp 的 1:1 移植）。
// 三种特效：
//   bomb  — r = sqrt(elapsedTicks+1)*bomb_range，elapsedTicks%2==0 时征服圆内格 + 击杀圆内敌兵，
//           elapsedTicks>8 消亡。
//   mine  — elapsedTicks>=600 起每 12 tick 探测敌方兵引爆（生成爆炸特效）；elapsedTicks>=3600 超时引爆。
//   laser — 沿 angle 逐格延伸，遇界或「进入非己方陆地」停止；击杀线段 ±0.1 内敌兵；
//           length < lst+2 时尖端格征服，否则每 tick +2 增长。
// 击杀一律经 markDead 记死亡事件（tick 末由 DeathSystem 统一销毁 + 生成死亡效果），
// 避免遍历中销毁实体。击杀者位置 = 特效创建者快照（原版 &army[creator_id]）。
#include "sim/systems/EffectSystem.h"

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

// 特效求解上下文：Simulation（生成爆炸特效）+ MoveContext（地图/势力/RNG/死亡事件/空间哈希）。
struct EffectCtx {
    Simulation& sim;
    MoveContext move;          // makeContext 构建（ttime = 本 tick 的 tickCount）
    double maxArmySize = 0;    // 空间查询搜索半径 / 包围盒扩展用
};

// 最大兵碰撞半径 = baseSize * max(sizeMult)（炸弹搜索、激光击杀包围盒需覆盖最大兵）。
double maxArmyRadius(const Config& cfg) {
    double m = 0;
    for (const auto& u : cfg.units) m = std::max(m, cfg.army.baseSize * u.sizeMult);
    return m;
}

// ---- 爆炸（type0，原版 Effect::solve0）----
void solveBomb(EffectCtx& ctx, entt::entity e, std::vector<entt::entity>& toDestroy) {
    auto& reg = ctx.move.registry;
    const auto& pos = reg.get<comp::Position>(e);
    const auto& fid = reg.get<comp::FactionId>(e);
    const auto& creator = reg.get<comp::Creator>(e);
    const auto& params = reg.get<comp::EffectParams>(e);
    const int elapsedTicks = ctx.move.ttime - reg.get<comp::EffectTimer>(e).createdTick;
    const auto& cfg = ctx.move.config;

    const double radius = std::sqrt(elapsedTicks + 1.0) * params.p0;  // p0 = bomb_range
    if (elapsedTicks % cfg.effect.bomb.conquerEveryTicks == 0) {
        // 征服：以 (x,y) 为圆心、半径 radius 内所有格（判定用格心距 < radius）。
        // 循环上界：原版用 double 比较 i < min(Map_x, x+radius+1)，等价于整数上界
        // ceil(min(Map_x, x+radius+1))——x+radius+1 非整时多跑一格（含恰为整数时的 min 夹取）。
        // 改用等值整数上界，迭代集合、征服/RNG 顺序完全不变（§2.6 回填语义），更清晰更快。
        // （2026-08 已用 build/landwar.exe --headless --seed 42 --ticks 20000 双向验证
        //   state_hash 逐字节一致，行为无漂移。）
        const int xMin = std::max(0, static_cast<int>(pos.x - radius));
        const int yMin = std::max(0, static_cast<int>(pos.y - radius));
        const int xMax =
            std::min(ctx.move.map.width(), static_cast<int>(std::ceil(pos.x + radius + 1.0)));
        const int yMax =
            std::min(ctx.move.map.height(), static_cast<int>(std::ceil(pos.y + radius + 1.0)));
        for (int i = xMin; i < xMax; ++i) {
            for (int j = yMin; j < yMax; ++j) {
                if (math::distance(i + 0.5, j + 0.5, pos.x, pos.y) < radius) {
                    conquerAt(ctx.move, i, j, fid.value, creator.unitType);  // P11：占领 credit=创建者
                }
            }
        }
        // 击杀：圆内敌方活兵（击杀者 = 创建者快照）。P9：子弹不受特效伤害（仅自身规则支配）。
        for (auto a : ctx.move.spatialHash.queryCircle(reg, pos.x, pos.y, radius)) {
            if (reg.all_of<comp::Dead>(a)) continue;
            if (reg.all_of<comp::Projectile>(a)) continue;
            if (reg.get<comp::FactionId>(a).value == fid.value) continue;
            const auto& armyPos = reg.get<comp::Position>(a);
            markDead(ctx.move, a, armyPos.x, armyPos.y, creator.x, creator.y, creator.factionId,
                     creator.unitType);  // P11：击杀 credit=创建者
        }
    }
    if (elapsedTicks > cfg.effect.bomb.lifetimeTicks) toDestroy.push_back(e);  // 消亡
}

// ---- 地雷（type1，原版 Effect::solve1）----
void solveMine(EffectCtx& ctx, entt::entity e, std::vector<entt::entity>& toDestroy) {
    auto& reg = ctx.move.registry;
    const auto& pos = reg.get<comp::Position>(e);
    const auto& fid = reg.get<comp::FactionId>(e);
    const auto& creator = reg.get<comp::Creator>(e);
    const int elapsedTicks = ctx.move.ttime - reg.get<comp::EffectTimer>(e).createdTick;
    const auto& mineCfg = ctx.move.config.effect.mine;
    const auto& f7 = ctx.move.config.factions[7];

    const double radius = mineCfg.radius;
    bool explode = false;
    // P7/P8：势力增益（地雷超时 × mineTimeoutMult；爆炸半径 × bombRadiusMult——大爆炸科技同时
    // 作用于地雷引爆/超时爆炸，基线乘数 1.0 → 恒等，行为不变）。
    const auto& fmodsMine = ctx.move.factions[static_cast<size_t>(fid.value)].mods;
    if (elapsedTicks >= mineCfg.armTicks && elapsedTicks % mineCfg.checkEveryTicks == 0) {
        // 探测：敌方兵距雷 < radius*1.1 + 兵size → 引爆（生成爆炸特效）。
        // Phase 9 修原版怪癖：首个命中即 break → 每雷只产生一个爆炸（原版无 break，
        // 多个敌兵在范围内会产生多个爆炸特效）。
        const double searchRadius = radius * mineCfg.triggerRadiusMult + ctx.maxArmySize;
        for (auto a : ctx.move.spatialHash.queryCircle(reg, pos.x, pos.y, searchRadius)) {
            if (reg.all_of<comp::Dead>(a)) continue;
            if (reg.all_of<comp::Projectile>(a)) continue;  // P9：子弹不触发地雷
            if (reg.get<comp::FactionId>(a).value == fid.value) continue;
            const auto& armyPos = reg.get<comp::Position>(a);
            const double armyRadius = reg.get<comp::Collider>(a).radius;
            if (math::distance(armyPos.x, armyPos.y, pos.x, pos.y)
                < radius * mineCfg.triggerRadiusMult + armyRadius) {
                const double bombRadius =
                    (fid.value == 7 ? f7.mineTriggerRadius : mineCfg.triggerBombRadius)
                    * fmodsMine.bombRadiusMult;
                SpawnSystem::spawnEffect(ctx.sim, pos.x, pos.y, fid.value, EffectType::bomb,
                                         bombRadius, creator);
                explode = true;
                break;  // 每雷只爆一次
            }
        }
    }
    if (elapsedTicks >= static_cast<int>(mineCfg.timeoutTicks * fmodsMine.mineTimeoutMult)) {
        // 超时自爆：爆炸半径统一为**引爆半径**（2026-08 用户定夺；原版超时更大 1.3 / 紫7 1.95）。
        const double bombRadius =
            (fid.value == 7 ? f7.mineTriggerRadius : mineCfg.triggerBombRadius)
            * fmodsMine.bombRadiusMult;
        SpawnSystem::spawnEffect(ctx.sim, pos.x, pos.y, fid.value, EffectType::bomb, bombRadius,
                                 creator);
        explode = true;
    }
    if (explode) toDestroy.push_back(e);
}

// ---- 激光（type2，原版 Effect::solve2）----
void solveLaser(EffectCtx& ctx, entt::entity e, std::vector<entt::entity>& toDestroy) {
    auto& reg = ctx.move.registry;
    const auto& pos = reg.get<comp::Position>(e);
    const auto& fid = reg.get<comp::FactionId>(e);
    const auto& creator = reg.get<comp::Creator>(e);
    auto& params = reg.get<comp::EffectParams>(e);
    const int elapsedTicks = ctx.move.ttime - reg.get<comp::EffectTimer>(e).createdTick;
    const auto& laserCfg = ctx.move.config.effect.laser;

    // P7：激光寿命/长度/宽度 × 势力增益（基线各乘数 1.0 → 恒等，行为不变）。
    const auto& fmods = ctx.move.factions[static_cast<size_t>(fid.value)].mods;
    const double durMult = fmods.laserDurationMult;
    const double lenMult = fmods.laserLengthMult;
    if (elapsedTicks > static_cast<int>(laserCfg.durationTicks * durMult)) {  // 求解前先判寿命
        toDestroy.push_back(e);
        return;
    }

    double& angle = params.p0;        // 原版 lsdouble1（撞角时 find_next_xy 随机改写，保留）
    double& finalLength = params.p1;  // 原版 lsdouble2
    double& lstLength = params.p2;    // 原版 lsdouble3
    if (elapsedTicks == 0) lstLength = 0;

    const double totalLength = laserCfg.length * lenMult;
    double remLength = totalLength;
    double originX = pos.x;
    double originY = pos.y;
    // 默认尖端格 = 特效所在格（原版 &mmap[(int)x][(int)y]，含越界哨兵列；新版夹取到界内）。
    const auto& defaultCell =
        ctx.move.map.at(std::clamp(static_cast<int>(pos.x), 0, ctx.move.map.width() - 1),
                        std::clamp(static_cast<int>(pos.y), 0, ctx.move.map.height() - 1));
    const MapCell* goalCell = &defaultCell;
    while (remLength > 0) {
        const int boundaryCode =
            math::findNextXY(originX, originY, angle, remLength, ctx.move.rng);
        if (boundaryCode < 0 || boundaryCode > 3) break;
        if (boundaryCode == 0 && originX <= 0) break;
        if (boundaryCode == 1 && originY <= 0) break;
        if (boundaryCode == 2 && originX >= ctx.move.map.width()) break;
        if (boundaryCode == 3 && originY >= ctx.move.map.height()) break;
        if (boundaryCode == 0)
            goalCell = &ctx.move.map.at(static_cast<int>(originX - kEps), static_cast<int>(originY));
        if (boundaryCode == 1)
            goalCell = &ctx.move.map.at(static_cast<int>(originX), static_cast<int>(originY - kEps));
        if (boundaryCode == 2)
            goalCell = &ctx.move.map.at(static_cast<int>(originX + kEps), static_cast<int>(originY));
        if (boundaryCode == 3)
            goalCell = &ctx.move.map.at(static_cast<int>(originX), static_cast<int>(originY + kEps));
        if (goalCell->land && fid.value != goalCell->belongi) break;  // 进入非己方陆地停止
    }
    const double length = totalLength - remLength;  // 本 tick 光束实际走过的长度

    // 击杀：到线段（起点 -> angle,length）垂距 <= 兵size+0.1 的敌方活兵。
    // 用包围盒候选（线段 AABB 按 maxArmySize+0.1 扩展）再精确过滤，等价原版全量扫描。
    const double killWidth = laserCfg.killExtraRadius * fmods.laserWidthMult;  // 激光变宽（P7）
    const double endX = pos.x + std::cos(angle) * length;
    const double endY = pos.y + std::sin(angle) * length;
    const double margin = ctx.maxArmySize + killWidth;
    const double boxX0 = std::min(pos.x, endX) - margin, boxX1 = std::max(pos.x, endX) + margin;
    const double boxY0 = std::min(pos.y, endY) - margin, boxY1 = std::max(pos.y, endY) + margin;
    for (auto a : ctx.move.spatialHash.queryAABB(reg, boxX0, boxY0, boxX1, boxY1)) {
        if (reg.all_of<comp::Dead>(a)) continue;
        if (reg.all_of<comp::Projectile>(a)) continue;  // P9：激光不杀子弹
        if (reg.get<comp::FactionId>(a).value == fid.value) continue;
        const auto& armyPos = reg.get<comp::Position>(a);
        const double dist =
            math::pointDistanceFromSegment(armyPos.x, armyPos.y, pos.x, pos.y, angle, length);
        if (dist <= reg.get<comp::Collider>(a).radius + killWidth) {
            markDead(ctx.move, a, armyPos.x, armyPos.y, creator.x, creator.y, creator.factionId,
                     creator.unitType);  // P11：击杀 credit=创建者
        }
    }

    // 长度渐进：length < lst+2（光束提前停下，如遇敌领土/边界）→ 尖端格征服；
    // 否则本 tick 至多增长 2（原版「增长时不征服」语义，见 §2.6 回填）。
    const double extendLimit = lstLength + laserCfg.extendPerTick;
    if (length < extendLimit) {
        conquerAt(ctx.move, goalCell->x, goalCell->y, fid.value, creator.unitType);  // P11：credit=创建者
        finalLength = length;
    } else {
        finalLength = extendLimit;
    }
    lstLength = finalLength;
}

}  // namespace

void EffectSystem::update(Simulation& sim) {
    auto& reg = sim.registry();
    // 快照特效列表：求解中新增的特效（地雷引爆的爆炸）下一 tick 再参与，保证确定性。
    // P9 确定性：按实体 id 降序迭代（与存储序无关 → 读档后 RNG/征服顺序与直跑一致）。
    auto view = reg.view<comp::EffectTypeId, comp::EffectTimer, comp::EffectParams>();
    std::vector<entt::entity> effects(view.begin(), view.end());
    std::sort(effects.begin(), effects.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) > entt::to_integral(b);
    });
    if (effects.empty()) return;

    // 特效查询需要军队移动后的位置：重建空间哈希。
    sim.spatialHash().build(reg, sim.map().width(), sim.map().height());

    EffectCtx ctx{sim, MovementSystem::makeContext(sim), maxArmyRadius(sim.config())};
    std::vector<entt::entity> toDestroy;
    for (auto e : effects) {
        if (!reg.valid(e)) continue;
        switch (reg.get<comp::EffectTypeId>(e).type) {
            case EffectType::bomb: solveBomb(ctx, e, toDestroy); break;
            case EffectType::mine: solveMine(ctx, e, toDestroy); break;
            case EffectType::laser: solveLaser(ctx, e, toDestroy); break;
        }
    }
    for (auto e : toDestroy) {
        if (reg.valid(e)) reg.destroy(e);
    }
}

}  // namespace lw
