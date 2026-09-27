// ConquestRules.h — 友/敌与"可占领"判定的唯一入口（兵运动规范 + 盟友系统）。
// 只读 Faction.allianceId / Faction.mods 与 MapCell 状态；纯函数、无 RNG、无副作用。
// 所有系统共用这一份实现，不得在别处另写"同势力才友"或"不同势力就占"的分支
//（见 .docs/兵运动与盟友系统开发文档.md §3）。
#pragma once

#include <cstddef>
#include <vector>

#include "world/Faction.h"
#include "world/Map.h"

namespace lw {

// 盟友关系：a != b 且同属一个联盟（allianceId >= 0）。自身、越界、无联盟 → false。
inline bool areAllied(const std::vector<Faction>& factions, int a, int b) {
    if (a == b || a < 0 || b < 0) return false;
    if (a >= static_cast<int>(factions.size()) || b >= static_cast<int>(factions.size()))
        return false;
    const int group = factions[static_cast<std::size_t>(a)].allianceId;
    return group >= 0 && group == factions[static_cast<std::size_t>(b)].allianceId;
}

// 敌我：a != b 且非盟友。中立（0）与任何非盟友仍算敌方（保持现状 "!=" 语义）。
inline bool areEnemies(const std::vector<Faction>& factions, int a, int b) {
    if (a < 0 || b < 0) return false;
    return a != b && !areAllied(factions, a, b);
}

// 势力 factionId 的 unitType 兵能否占领 index 格（"不可占领格"的唯一判定）。
// 海 / 盟友陆 / 敌方陆且该兵种带「禁征服」特性（密集防御）→ false；
// 己方陆与中立陆 → true。unitType < 0 = 不施加兵种机制限制（首都初始化等无兵种来源）。
inline bool canConquerCell(const Map& map, const std::vector<Faction>& factions, int factionId,
                           int unitType, int index) {
    if (factionId < 0 || factionId >= static_cast<int>(factions.size())) return false;
    if (index < 0 || index >= map.cellCount()) return false;
    const MapCell& cell = map.atIndex(index);
    if (!cell.land) return false;                                    // 海（海洋闸门单独处理）
    if (cell.belongi == factionId) return true;                      // 己方：占领是 no-op
    if (areAllied(factions, factionId, cell.belongi)) return false;  // 盟友：不可占领
    // 敌方陆且兵种带「禁征服」→ 不可占领；中立（0）不在此列（仍可扩张）。
    if (unitType >= 0 && unitType < kArmyTypeCount && cell.belongi > 0
        && factions[static_cast<std::size_t>(factionId)]
               .mods.noEnemyConquer[static_cast<std::size_t>(unitType)])
        return false;
    return true;
}

}  // namespace lw
