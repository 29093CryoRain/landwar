#include "sim/systems/CapitalSystem.h"

#include <cstdint>

#include "core/Simulation.h"
#include "world/Map.h"

namespace lw {

namespace {

// 城市是否归 faction 所有（ownerId 校验；城市注册表永不回收 → cityId 恒有效，仅 owner 会变）。
bool ownsCity(const Simulation& sim, int factionId, int cityId) {
    if (cityId < 0 || cityId >= sim.map().cityCount()) return false;
    return sim.map().city(cityId).ownerId == factionId;
}

}  // namespace

int CapitalSystem::pickDesignatedCity(const Simulation& sim, int factionId) {
    const auto& f = sim.faction(factionId);
    int best = -1;
    std::uint64_t bestTick = ~0ULL;
    for (int cid : f.cityIds) {
        const auto& city = sim.map().city(cid);
        if (city.lastCapturedTick < bestTick ||
            (city.lastCapturedTick == bestTick && cid < best)) {
            bestTick = city.lastCapturedTick;
            best = cid;
        }
    }
    return best;
}

void CapitalSystem::update(Simulation& sim) {
    // 防空：默认 Simulation（未 init）factions 为空 → 直接跳过（tick 冒烟测试依赖此，
    // 与 EconomySystem/detectAnnihilationAndUnification 同款防线）。
    if (sim.factions().size() != static_cast<size_t>(kFactionTotal)) return;
    const std::uint64_t now = sim.tickCount();
    const auto& cfg = sim.config();
    for (int fid = 1; fid <= kPlayerFactionCount; ++fid) {
        auto& f = sim.faction(fid);
        if (!f.alive) continue;
        CapitalState& cs = f.capitalState;

        // 1) 正式首都整城易主 → 沦陷（无正式首都）。首都仍在 → 本 tick 无事
        //    （存在正式首都时不能迁都）；刚沦陷 → 本 tick 也跳过指定/迁都（下 tick 处理）。
        if (cs.capitalCityId >= 0) {
            if (!ownsCity(sim, fid, cs.capitalCityId)) {
                cs.capitalCityId = -1;
                cs.lostTick = now;
                cs.immediateRelocate = false;
                sim.pushEvent(GameEvent{GameEventKind::CapitalLost, fid, 0,
                                        formatEventTime(now, cfg.sim.tickRate) + " 势力 "
                                            + factionShortName(fid) + " 的首都被攻破",
                                        now});
            }
            continue;
        }

        // 2) 无正式首都（沦陷窗口内/过期）：确保候补指定。AI 自动指定（最久未被攻破）；
        //    玩家势力不自动指定——由 DebugPanel"指定首都"经 setDesignatedCapital 设置。
        if (!ownsCity(sim, fid, cs.designatedCityId)) {
            if (f.aiId == 0) cs.designatedCityId = pickDesignatedCity(sim, fid);
        }

        // 3) 窗口判定与迁都（仅沦陷后；lostTick>0）。
        if (cs.lostTick > 0) {
            const bool windowActive = (now - cs.lostTick < cfg.capital.relocationDelayTicks);
            const bool canRelocate = cs.immediateRelocate || !windowActive;
            if (canRelocate && ownsCity(sim, fid, cs.designatedCityId)) {
                // 窗口期满（或立即迁都态）：正式迁都到候补。不重置 lostTick 之外的状态。
                cs.capitalCityId = cs.designatedCityId;
                cs.designatedCityId = -1;
                cs.lostTick = 0;
                cs.immediateRelocate = false;
            } else if (!windowActive && !ownsCity(sim, fid, cs.designatedCityId)) {
                // 窗口过期仍无有效指定 → immediateRelocate（此后指定任意城即立即迁都）。
                cs.designatedCityId = -1;
                cs.immediateRelocate = true;
            }
        }
    }
}

}  // namespace lw
