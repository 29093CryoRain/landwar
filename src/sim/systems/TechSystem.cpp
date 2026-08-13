// TechSystem.cpp — 科技系统实现（开发计划 P8，思路"科技细节"节为准）。
#include "sim/systems/TechSystem.h"

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "sim/components.h"
#include "world/Faction.h"

namespace lw {

namespace {

// 当前可升级科技（level < maxLevel）下标列表（config 顺序）。纯函数。
std::vector<int> availableTechs(const Config& cfg, const std::vector<int>& levels) {
    std::vector<int> out;
    for (size_t i = 0; i < cfg.tech.techs.size(); ++i) {
        const int lv = (i < levels.size()) ? levels[i] : 0;
        if (lv < static_cast<int>(cfg.tech.techs[i].levels.size()))
            out.push_back(static_cast<int>(i));
    }
    return out;
}

// 玩家候选采样（不放回，rng.get 顺序固定 → 确定性）。填充到固定长度（-1 = 空位）。
std::vector<int> sampleCandidates(Simulation& sim, Faction& f, const Config& cfg) {
    const std::vector<int> avail = availableTechs(cfg, f.tech.levels);
    const int want = std::min(cfg.tech.playerCandidateCount,
                              static_cast<int>(avail.size()));
    std::vector<int> pool = avail;
    std::vector<int> candidates;
    for (int i = 0; i < want; ++i) {
        // 注意：Rng::get(n) 返回 [0, n]（含端点）→ 索引数组须用 get(size-1)。
        const int pick = sim.rng().get(static_cast<int>(pool.size()) - 1);
        candidates.push_back(pool[static_cast<size_t>(pick)]);
        pool[static_cast<size_t>(pick)] = pool.back();
        pool.pop_back();
    }
    candidates.resize(static_cast<size_t>(cfg.tech.playerCandidateCount), -1);
    return candidates;
}

// 行军重缩放：科技 buff 为 UnitSpeedMult 时，把该势力现有兵（非子弹）的 Speed × (新/旧 mods)。
// 每 tick 移动量在出生/下海/登陆时已含速度乘数 → 研究后按比率一次缩放即对已存兵即时生效。
void rescaleExistingArmySpeed(Simulation& sim, int factionId, const Config::Tech::Level& lv,
                              const FactionMods& oldMods) {
    auto& reg = sim.registry();
    std::vector<int> types;
    if (lv.param < 0) {
        for (int t = 0; t < kArmyTypeCount; ++t) types.push_back(t);
    } else {
        types.push_back(lv.param);
    }
    for (int t : types) {
        const double oldM = oldMods.speedMult[static_cast<size_t>(t)];
        const double newM = sim.faction(factionId).mods.speedMult[static_cast<size_t>(t)];
        if (oldM <= 0.0) continue;
        const double ratio = newM / oldM;
        if (ratio == 1.0) continue;
        for (auto e : reg.view<comp::FactionId, comp::UnitType, comp::Speed>()) {
            if (reg.all_of<comp::Projectile>(e)) continue;  // 子弹不受行军
            if (reg.get<comp::FactionId>(e).value != factionId) continue;
            if (static_cast<int>(reg.get<comp::UnitType>(e).type) != t) continue;
            reg.get<comp::Speed>(e).value *= ratio;
        }
    }
}

}  // namespace

void TechSystem::update(Simulation& sim) {
    const Config& cfg = sim.config();
    if (cfg.tech.techs.empty()) return;  // 无科技表（默认 config）→ 系统惰性，零 RNG
    for (int idx : sim.turnOrder()) {
        const int fid = idx + 1;  // turnOrder 存 0..7 → 势力 id 1..8
        Faction& f = sim.faction(fid);
        if (!f.alive) continue;
        if (f.tech.researchPending) continue;  // 玩家待选：等 choosePlayerTech 结算（暂停中）

        // 1. 科技点累积：每座 n 级城每 tick 产 n × pointsPerCityLevel × techGainMult（0 RNG）。
        double cityLevelSum = 0.0;
        for (int cid : f.cityIds) cityLevelSum += sim.map().city(static_cast<size_t>(cid)).level;
        f.tech.points += cityLevelSum * cfg.tech.pointsPerCityLevel * f.mods.techGainMult;

        // 2. 阈值触发：获得一次科研机会（思路：到达阈值清空 + 科研机会，阈值提升）。
        if (f.tech.points < f.tech.threshold) continue;
        if (f.aiId == 1) {
            // 玩家：只采样候选 + 置待选；点/阈值的结算统一在 choosePlayerTech（避免双重递增）。
            f.tech.candidates = sampleCandidates(sim, f, cfg);
            f.tech.researchPending = true;
        } else {
            // AI：立即清空科技点 + 阈值提升 + 随机选一个可升级科技（rng.get，1 次 RNG）。
            f.tech.points = 0.0;
            f.tech.threshold += cfg.tech.thresholdStep;
            const std::vector<int> avail = availableTechs(cfg, f.tech.levels);
            if (avail.empty()) continue;  // 无可升级科技：科研机会浪费（阈值已提升）
            // 注意：Rng::get(n) 返回 [0, n]（含端点）→ 索引数组须用 get(size-1)。
            const int pick = avail[sim.rng().get(static_cast<int>(avail.size()) - 1)];
            applyTech(sim, fid, pick);
        }
    }
}

void TechSystem::applyTech(Simulation& sim, int factionId, int techIndex) {
    const Config& cfg = sim.config();
    if (techIndex < 0 || techIndex >= static_cast<int>(cfg.tech.techs.size())) return;
    Faction& f = sim.faction(factionId);
    const auto& def = cfg.tech.techs[static_cast<size_t>(techIndex)];

    // 已满级 → 不应用（防御；候选/选择不至此）。
    const int curLevel =
        (techIndex < static_cast<int>(f.tech.levels.size())) ? f.tech.levels[techIndex] : 0;
    if (curLevel >= static_cast<int>(def.levels.size())) return;

    const FactionMods oldMods = f.mods;  // 行军重缩放 ratio 用

    // 1. 升级等级 → 重建 buffs（initial + tech）→ 聚合 mods。
    if (techIndex >= static_cast<int>(f.tech.levels.size()))
        f.tech.levels.resize(cfg.tech.techs.size(), 0);
    const int newLevel = ++f.tech.levels[techIndex];
    f.rebuildBuffsFromDef(cfg.factions[static_cast<size_t>(factionId)], cfg);

    // 2. 默认 AI 兵种偏好加成（思路：非全兵种生效科技每级 +preferencePerLevel）。
    if (cfg.tech.preferencePerLevel != 0.0) {
        for (int u : def.preferenceUnits)
            f.unitPreference[static_cast<size_t>(u)] += cfg.tech.preferencePerLevel;
    }

    // 3. 行军：重缩放现有兵 Speed（对已存兵即时生效）。
    const auto& lv = def.levels[static_cast<size_t>(newLevel - 1)];
    if (lv.type == BuffType::UnitSpeedMult)
        rescaleExistingArmySpeed(sim, factionId, lv, oldMods);

    // 4. 事件（消息面板显示；data = 科技下标）。
    sim.pushEvent(GameEvent{GameEventKind::TechAcquired, factionId, techIndex,
                            formatEventTime(sim.tickCount(), cfg.sim.tickRate) + " 势力 "
                                + factionShortName(factionId) + " 获得科技 " + def.name + "("
                                + std::to_string(newLevel) + "级)",
                            sim.tickCount()});
}

}  // namespace lw
