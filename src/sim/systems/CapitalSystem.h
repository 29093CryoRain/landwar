// CapitalSystem.h — P15 首都迁都状态机（开发计划 P15，思路 9.2）。
// 每 tick 推进每势力的迁都状态（Faction::capitalState）：
//   1. 正式首都整城易主（ownerId 变更）→ 沦陷：capitalCityId=-1、lostTick=now、
//      immediateRelocate=false，发 CapitalLost 消息（P4 通道）。
//   2. 沦陷窗口（now-lostTick < capital.relocationDelayTicks）：AI（aiId=0）确保指定
//      最久未被攻破的己方城市；玩家势力（aiId=1）由 DebugPanel"指定首都"经
//      Simulation::setDesignatedCapital 指定。改指/指定城被攻破均不重置 lostTick。
//   3. 窗口期满：有效指定 → 正式迁都（capitalCityId=它、清 designated/lostTick）；
//      无效 → 清 designated、immediateRelocate=true（此后指定任意城即立即迁都）。
// 有正式首都 → 不动作（存在正式首都时不能迁都）。
// **确定性、不消耗 Rng**：AI 指定为纯函数（lastCapturedTick 最小、并列取 id 小者）。
// 首都沦陷与势力灭亡可能同 tick —— 本系统在 DeathSystem 之后、灭亡检测之前运行，顺序固定。
#pragma once

namespace lw {

class Simulation;

class CapitalSystem {
public:
    // 每 tick 推进全部势力的迁都状态机。纯逻辑、无 RNG。
    static void update(Simulation& sim);

    // P15：AI 指定候补新都 = 选 lastCapturedTick 最小（最久未被攻破）的己方城市
    //（并列取 id 小者）。纯函数（无 RNG），可独立单测；返回 -1 = 无己方城市。
    static int pickDesignatedCity(const Simulation& sim, int factionId);
};

}  // namespace lw
