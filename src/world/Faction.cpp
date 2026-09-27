#include "world/Faction.h"

#include <algorithm>
#include <cassert>

#include "sim/ConquestRules.h"
#include "world/Map.h"

namespace lw {

namespace {
// P13/P12 + B8：城市全部基建格（形状按密铺解析）是否同属 factionId 一方——
// "本势力"或"本势力的盟友"（同一联盟）。整城易主判定即此。
// 注意调用点在 land 归属（cell.belongi）已改为本势力之后，故刚征服的格已计入。
bool allBaseCellsInAlliance(const Map& map, const std::vector<Faction>& factions, const City& city,
                            int factionId) {
    const std::vector<int> cells = map.cityCells(city);
    for (int idx : cells) {
        if (idx < 0) return false;
        const int owner = map.atIndex(idx).belongi;
        // 中立（0）与任何非盟友仍算敌方（无联盟者之间也互不友好）→ 不满足整城易主。
        if (owner != factionId && !areAllied(factions, factionId, owner)) return false;
    }
    return true;
}
}  // namespace

void Faction::initFromDef(const Config::Faction& def, const Config& cfg) {
    id = def.id;
    name = def.name;
    selected = false;
    alive = (id != kNeutralFaction);
    aiId = 0;  // Options 会覆盖（Simulation::initFactions），此处给默认
    color = def.color;
    cityCount = 0;
    landCount = 0;
    numArmyProduced = 0;
    economy = cfg.economy.initialEconomy;  // 留存值起点（库存）
    economyRate = 0.0;
    techRate = 0.0;
    maxCityLevel = 0.0;
    freeArmyChance = 0.0;
    bombRadiusBonus = 0.0;
    mineTriggerBombRadiusBonus = 0.0;
    spawnAngle = 0.0;
    spawnAngleSet = false;
    for (int t = 0; t < kArmyTypeCount; ++t) {
        // P11 改版（用户定夺）：势力特色**不再影响兵种价格**——armyCost = 定义 baseCost（各势力
        // 同价，原 costDiv 折扣已移除）；势力特色改由 unitPreference 驱动默认 AI 产兵分配。
        armyCost[t] = cfg.units[t].cost;
        producedCost[t] = 0.0;
    }
    unitPreference = def.unitPreference;  // 兵种偏好系数（默认 AI 排序键用）
    cityIds.clear();
    // P7：把势力定义修饰符字段翻译为初始 buff（source="faction"）→ 聚合出 mods。
    buffs = initialFactionBuffs(def);
    // P8：初始化科技状态（阈值 = 初始阈值；levels 对齐 config.techs）。
    tech = TechState{};
    tech.threshold = cfg.tech.thresholdBase;
    tech.levels.assign(cfg.tech.techs.size(), 0);
    recomputeMods();
    bombRadiusBonus = mods.bombExplosionRadiusAdd - 1.0;
    mineTriggerBombRadiusBonus = mods.mineExplosionRadiusAdd - 1.0;
}

void Faction::rebuildBuffsFromDef(const Config::Faction& def, const Config& cfg) {
    // P8：buffs = 势力定义初始 buff + 科技 buff（读档后从 tech.levels 重建；mods 随之聚合）。
    std::vector<Buff> buffs2 = initialFactionBuffs(def);
    const std::vector<Buff> tb = techBuffs(cfg, this->tech.levels);
    buffs2.insert(buffs2.end(), tb.begin(), tb.end());
    buffs = std::move(buffs2);
    recomputeMods();
    bombRadiusBonus = mods.bombExplosionRadiusAdd - 1.0;
    mineTriggerBombRadiusBonus = mods.mineExplosionRadiusAdd - 1.0;
}

void Faction::recomputeMods() {
    mods = computeMods(buffs);
    freeArmyChance = mods.freeArmyChance;
}

int Faction::techLevel() const {
    int sum = 0;
    for (int l : tech.levels) sum += l;
    return sum;
}

void Faction::insertCity(int cityId, double level) {
    if (id <= 0 || id >= kMaxFactionCount) return;
    if (cityId < 0) return;
    for (int cid : cityIds)
        if (cid == cityId) return;  // 已存在，去重
    cityIds.push_back(cityId);
    cityCount = static_cast<int>(cityIds.size());
    maxCityLevel = std::max(maxCityLevel, level);
    // 不变量（工程改进）：cityCount 恒等于 cityIds.size()（双簿记防漂移）。
    assert(cityCount == static_cast<int>(cityIds.size()));
}

void Faction::removeCity(int cityId, double level) {
    if (cityIds.empty() || id <= 0 || id >= kMaxFactionCount) return;
    for (size_t i = 0; i < cityIds.size(); ++i) {
        if (cityIds[i] == cityId) {
            // 与原版一致：与末位交换再删除（顺序无关）。
            std::swap(cityIds[i], cityIds.back());
            cityIds.pop_back();
            cityCount = static_cast<int>(cityIds.size());
            if (level >= maxCityLevel) maxCityLevel = 0.0;
            assert(cityCount == static_cast<int>(cityIds.size()));  // 不变量同上
            break;
        }
    }
}

void Faction::recomputeMaxCityLevel(const Map& map) {
    maxCityLevel = 0.0;
    for (int cid : cityIds) {
        if (cid >= 0 && cid < map.cityCount())
            maxCityLevel = std::max(maxCityLevel, map.city(cid).level);
    }
}

void Faction::conquerIndex(ConquerContext& ctx, int index) {
    // 2026-09 用户定夺：征服不再依赖"兵最终停在哪格"（旧 movedArmy 的终点格补占已删除），
    // 改为占目标格时一并检查调用方声明的 originIndex（兵进入前的格）——凡能占的一起占。
    // 单格征服收敛在这里；origin 用同一套 land/同势力/整城易主规则，海格与无效下标自然忽略。
    const int originIndex = ctx.originIndex;
    const auto conquerOne = [&](int idx) {
        if (idx < 0 || idx >= ctx.map.cellCount()) return;
        // 不可占领格（海 / 盟友 / 禁征服敌土）在此统一拦下——唯一判定入口。
        if (!canConquerCell(ctx.map, ctx.factions, this->id, ctx.attackerUnitType, idx)) return;
        MapCell& cell = ctx.map.atIndex(idx);
        Faction& oldOwner = ctx.factions[static_cast<size_t>(cell.belongi)];
        if (&oldOwner == this) return;  // 同势力忽略

        if (cell.land) {
            oldOwner.landCount--;
            this->landCount++;
            cell.belongi = this->id;
        }
        // 城市易主（P13，思路 9.1；B8 用户反馈改为按联盟判定）：
        // 本格 land 归属已改为本势力 → 检测城市全部基建格是否同属本势力所在联盟：
        //   是 → 整城易主到**本次征服者**（只触发一次，非每格）；否 → 城市不转移。
        // 无联盟时"同联盟"退化为"同势力"，与旧行为逐位一致。
        if (cell.cityId >= 0) {
            City& city = ctx.map.city(cell.cityId);
            if (city.ownerId != this->id
                && allBaseCellsInAlliance(ctx.map, ctx.factions, city, this->id)) {
                Faction& curOwner = ctx.factions[static_cast<size_t>(city.ownerId)];
                curOwner.removeCity(city.id, city.level);
                curOwner.recomputeMaxCityLevel(ctx.map);
                this->insertCity(city.id, city.level);
                city.ownerId = this->id;
                city.lastCapturedTick = ctx.tick;
                // 任意定义了免费产兵概率的势力：每个配置的兵种概率分别判定。
                // init 阶段（freeArmyEnabled=false）跳过 → 无"开局免费兵"（用户定夺 2026-08）。
                if (ctx.freeArmyEnabled) {
                    for (int type = 0; type < kArmyTypeCount; ++type) {
                        const double chance =
                            this->mods.freeArmyChanceByType[static_cast<size_t>(type)]
                            * this->mods.freeArmyChanceMult;
                        if (chance <= 0.0 || !ctx.rng.chance(chance)) continue;
                        ctx.pendingSpawns.push_back(
                            PendingSpawn{type, this->id, city.centerX(), city.centerY()});
                    }
                }
            }
        }
    };
    conquerOne(index);
    if (originIndex >= 0 && originIndex != index) conquerOne(originIndex);
}

}  // namespace lw
