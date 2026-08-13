// Statistics.h — 统计系统（开发计划 P11，思路 8）。
// 按 (势力, 兵种) 记录击杀/占领/产兵，并提供势力/兵种维度汇总；截止到统一
// （cutoffTick>0 后 record* 全部忽略，冻结统计）。
// record* 为**纯计数**（无 RNG、不读 sim 状态）→ 启用统计不影响模拟确定性（验收要求）。
// 汇总（factionKills/…、unitKills/…）为派生数据：record* 期间增量维护，快照读档后
// 用 recompute() 从 perFactionUnit 重建（幂等）。人均（kills/produced、lands/produced）
// 由展示层派生（0 除保护在 UI）。
#pragma once

#include <array>
#include <cstdint>

#include "core/GameDefs.h"

namespace lw {

// 单 (势力, 兵种) 统计（击杀/占领/产兵）。
struct FactionUnitStat {
    int kills = 0;
    int lands = 0;
    int produced = 0;
};

// 全量统计（Simulation 持有，系统经 sim.stats() 访问）。
struct Statistics {
    std::array<std::array<FactionUnitStat, kArmyTypeCount>, kFactionTotal> perFactionUnit{};
    std::array<int, kFactionTotal> factionKills{};
    std::array<int, kFactionTotal> factionLands{};
    std::array<int, kFactionTotal> factionProduced{};
    std::array<int, kArmyTypeCount> unitKills{};
    std::array<int, kArmyTypeCount> unitLands{};
    std::array<int, kArmyTypeCount> unitProduced{};
    std::uint64_t cutoffTick = 0;  // 统一发生 tick；>0 后 record* 全部忽略（冻结统计）

    // 记录击杀（credit = 击杀者 (faction, unitType)）。中立(0)/越界索引忽略；截止后忽略。
    void recordKill(int factionId, int unitType) {
        if (!valid(factionId, unitType)) return;
        ++perFactionUnit[static_cast<size_t>(factionId)][static_cast<size_t>(unitType)].kills;
        ++factionKills[static_cast<size_t>(factionId)];
        ++unitKills[static_cast<size_t>(unitType)];
    }
    void recordLand(int factionId, int unitType) {
        if (!valid(factionId, unitType)) return;
        ++perFactionUnit[static_cast<size_t>(factionId)][static_cast<size_t>(unitType)].lands;
        ++factionLands[static_cast<size_t>(factionId)];
        ++unitLands[static_cast<size_t>(unitType)];
    }
    void recordProduced(int factionId, int unitType) {
        if (!valid(factionId, unitType)) return;
        ++perFactionUnit[static_cast<size_t>(factionId)][static_cast<size_t>(unitType)].produced;
        ++factionProduced[static_cast<size_t>(factionId)];
        ++unitProduced[static_cast<size_t>(unitType)];
    }

    // 从 perFactionUnit 重建汇总（快照读档后调用；幂等）。记录期间由 record* 增量维护。
    void recompute() {
        factionKills.fill(0);
        factionLands.fill(0);
        factionProduced.fill(0);
        unitKills.fill(0);
        unitLands.fill(0);
        unitProduced.fill(0);
        for (int fid = 1; fid < kFactionTotal; ++fid) {
            for (int ut = 0; ut < kArmyTypeCount; ++ut) {
                const auto& s = perFactionUnit[static_cast<size_t>(fid)][static_cast<size_t>(ut)];
                factionKills[static_cast<size_t>(fid)] += s.kills;
                factionLands[static_cast<size_t>(fid)] += s.lands;
                factionProduced[static_cast<size_t>(fid)] += s.produced;
                unitKills[static_cast<size_t>(ut)] += s.kills;
                unitLands[static_cast<size_t>(ut)] += s.lands;
                unitProduced[static_cast<size_t>(ut)] += s.produced;
            }
        }
    }

private:
    bool valid(int fid, int ut) const {
        if (cutoffTick > 0) return false;          // 截止后冻结
        if (fid <= 0 || fid >= kFactionTotal) return false;  // 不含中立 0 / 越界
        return ut >= 0 && ut < kArmyTypeCount;
    }
};

}  // namespace lw
