// TechSystem.h — 科技系统（开发计划 P8，思路"科技细节"）。
// 科技点 = 城市产出（每 tick 每座 n 级城产 n × pointsPerCityLevel × mods.techGainMult 点）；
// 阈值门槛：points >= threshold → 清空科技点 + 获得一次科研机会，threshold += thresholdStep。
// AI 势力（aiId!=1）到阈值立即随机选一个可升级科技（rng.get，1 次 RNG）；玩家势力（aiId==1）
// 采样候选（rng，不放回）置 researchPending，由 UI 三选一后经 Simulation::choosePlayerTech 结算。
// 科技 = 追加 source="tech:<id>" 的 buff（升级 = 用该级累计效果替换旧 buff）→ mods 聚合生效。
#pragma once

namespace lw {

class Simulation;

class TechSystem {
public:
    // 推进科技（tick 末尾调用，纯逻辑）：点累积 + 阈值触发 + AI 科研 / 玩家待选。确定性。
    static void update(Simulation& sim);

    // 应用一次科技取得：升级等级 → 重建 buffs/mods → 偏好加成 → （行军）重缩放现有兵 Speed →
    // 发 TechAcquired 事件。不消耗 Rng（选择/采样的 RNG 在调用方消费）。可被单测直接调用。
    static void applyTech(Simulation& sim, int factionId, int techIndex);
};

}  // namespace lw
