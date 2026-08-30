#include "world/Faction.h"

#include <algorithm>
#include <cassert>

#include "world/Map.h"

namespace lw {

namespace {
// P13/P12：城市全部基建格（形状按密铺解析）是否同属 factionId（整城易主判定）。
// 注意调用点在 land 归属（cell.belongi）已改为本势力之后，故刚征服的格已计入。
bool allBaseCellsOwned(const Map& map, const City& city, int factionId) {
    const std::vector<int> cells = map.cityCells(city);
    for (int idx : cells)
        if (idx < 0 || map.atIndex(idx).belongi != factionId) return false;
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
    freeArmyChance = def.freeArmyChance;
    bombRadiusBonus = def.bombRadiusBonus;
    mineTriggerBombRadiusBonus = def.mineTriggerBombRadiusBonus;
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
}

void Faction::rebuildBuffsFromDef(const Config::Faction& def, const Config& cfg) {
    // P8：buffs = 势力定义初始 buff + 科技 buff（读档后从 tech.levels 重建；mods 随之聚合）。
    std::vector<Buff> buffs2 = initialFactionBuffs(def);
    const std::vector<Buff> tb = techBuffs(cfg, this->tech.levels);
    buffs2.insert(buffs2.end(), tb.begin(), tb.end());
    buffs = std::move(buffs2);
    recomputeMods();
}

void Faction::recomputeMods() {
    mods = computeMods(buffs);
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

void Faction::conquer(ConquerContext& ctx, int x, int y) {
    // 保持旧 (x,y) 边界语义（x=width 或 y=height 须拒绝；转下标会落到别行）。
    if (x < 0 || x >= ctx.map.width() || y < 0 || y >= ctx.map.height()) return;
    conquerIndex(ctx, y * ctx.map.width() + x);
}

void Faction::conquerIndex(ConquerContext& ctx, int index) {
    if (index < 0 || index >= ctx.map.cellCount()) return;
    MapCell& cell = ctx.map.atIndex(index);
    Faction& oldOwner = ctx.factions[static_cast<size_t>(cell.belongi)];
    if (&oldOwner == this) return;  // 同势力忽略

    if (cell.land) {
        oldOwner.landCount--;
        this->landCount++;
        cell.belongi = this->id;
    }
    // 城市易主（P13，思路 9.1：全部基建地块同时被另一方占领才易主）。
    // 本格 land 归属已改为本势力 → 检测城市全部基建格是否同归本势力：
    //   是 → 整城易主（只触发一次，非每格）；否 → 城市不转移（部分/第三方占格只改土地归属）。
    if (cell.cityId >= 0) {
        City& city = ctx.map.city(cell.cityId);
        if (city.ownerId != this->id && allBaseCellsOwned(ctx.map, city, this->id)) {
            Faction& curOwner = ctx.factions[static_cast<size_t>(city.ownerId)];
            curOwner.removeCity(city.id, city.level);
            curOwner.recomputeMaxCityLevel(ctx.map);
            this->insertCity(city.id, city.level);
            city.ownerId = this->id;
            city.lastCapturedTick = ctx.tick;
            // 任意定义了免费产兵概率的势力：整城易主一次免费产兵机会。
            // init 阶段（freeArmyEnabled=false）跳过 → 无"开局免费兵"（用户定夺 2026-08）。
            if (ctx.freeArmyEnabled && this->freeArmyChance > 0.0
                && ctx.rng.chance(this->freeArmyChance * this->mods.freeArmyChanceMult)) {
                ctx.pendingSpawns.push_back(
                    PendingSpawn{0, this->id, city.centerX(), city.centerY()});
            }
        }
    }
}

}  // namespace lw
