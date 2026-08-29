// Faction.h — 势力运行时状态 + 征服/城市列表操作（翻新计划 §2.7 / §2.8，world/）。
// 定义（颜色/成本修正等）来自 Config::Faction；运行时状态在 initFromDef 中构建。
// P13（思路 9.1）：Faction::City 单格 {x,y,lastProduceArmyN} 改为 Map 注册表 City 实体
// （多格基建地块）；Faction 只持 cityIds 列表（注册表是唯一权威数据，避免结构分叉）。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/Config.h"
#include "core/GameDefs.h"
#include "core/Random.h"
#include "sim/Buff.h"

namespace lw {

class Map;     // 前向声明
class Faction; // 前向声明（ConquerContext 先于 Faction 定义引用它）

// 待产兵请求：势力8 攻占城市免费产兵（原版立即 add_army；新版暂记待办，
// Phase 3 由 SpawnSystem 统一落地，见翻新计划 §2.8）。
struct PendingSpawn {
    int type = 0;
    int factionId = 0;
    double x = 0.0;
    double y = 0.0;
};

// 首都迁都状态（P15，思路 9.2）。状态迁移由 CapitalSystem 每 tick 推进（见开发计划 P15）。
struct CapitalState {
    int capitalCityId = -1;          // 正式首都城市 id（-1 = 无正式首都）
    int designatedCityId = -1;       // 候补新都城市 id（-1 = 未指定）
    std::uint64_t lostTick = 0;      // 首都沦陷 tick（0 = 无沦陷窗口）
    bool immediateRelocate = false;  // 无指定且窗口过期 → 之后指定任意城即立即迁都
};

// P8 科技状态（思路"科技细节"）。levels 与 config.techs 对齐（下标即科技序，0=未拥有）。
// points/threshold 由 TechSystem 推进；researchPending/candidates 仅玩家势力用（AI 恒 false/空）。
// 科技 buffs 不入快照，读档由 techBuffs(cfg, levels) 重建；unitPreference 的科技偏好加成已入
// 快照（读档自然恢复，重建路径不重复加）。阈值到点"清空+科研机会"（researchPending / AI 立即选）。
struct TechState {
    double points = 0.0;
    double threshold = 0.0;      // initFromDef 置 cfg.tech.thresholdBase；每次科研 +thresholdStep
    std::vector<int> levels;      // 与 config.techs 对齐；0=未拥有
    bool researchPending = false; // 玩家待选（有科研机会未选，游戏暂停弹三选一）
    std::vector<int> candidates;  // 玩家候选 tech 下标（-1=空位；AI 恒空）
};

// 征服上下文：Simulation 注入 conquer 所需的一切（保持 Faction::conquer 可独立测试）。
struct ConquerContext {
    Map& map;
    std::vector<Faction>& factions;  // 下标即 id（0..8）
    Rng& rng;
    std::vector<PendingSpawn>& pendingSpawns;
    // 势力8 免费产兵开关：init 阶段征服首都时置 false（用户定夺 2026-08，去掉"开局免费兵"怪癖，
    // 保留"攻占城市 60% 免费产兵"特色）。其余征服路径默认 true（行为同原版）。
    bool freeArmyEnabled = true;
    std::uint64_t tick = 0;  // P13：当前逻辑帧（整城易主 lastCapturedTick 用）
};

class Faction {
public:
    // 从定义初始化运行时状态：成本 = 定义 baseCost（P11 改版后各势力同价）。
    void initFromDef(const Config::Faction& def, const Config& cfg);
    // 读档重建运行时 buffs + mods（P7：buffs 不入快照，恒可自定义重建；P8：initial + tech）。
    void rebuildBuffsFromDef(const Config::Faction& def, const Config& cfg);
    // 从当前 buffs 重算 mods 缓存（init/读档/科技取得后调用）。纯函数、无 RNG。
    void recomputeMods();

    // 城市列表操作（原版 insert_city / remove_city，见翻新计划 §2.8；P13 改为 cityId 列表）。
    void insertCity(int cityId);
    void removeCity(int cityId);

    // 征服（原版 conquer，见翻新计划 §2.8）。势力8 攻占城市时按概率记待产兵请求。
    void conquer(ConquerContext& ctx, int x, int y);
    // P12：按格下标征服（密铺统一路径；方 = 等价 conquer(ctx, x, y)）。含整城易主。
    void conquerIndex(ConquerContext& ctx, int index);

    // ---- 运行时状态 ----
    int id = 0;
    bool alive = false;
    int aiId = 0;  // 产兵 AI id（0=默认 AI，1=玩家；来自 Options，P1）
    std::array<int, 3> color = {127, 127, 127};
    int cityCount = 0;       // fcitys（始终 == cities.size()）
    int landCount = 0;       // flands
    int numArmyProduced = 0; // num_army_have_produced
    // 经济 = **留存值（库存）**（Phase 9 用户定夺）：每 tick 累加收益，产兵扣该兵成本（≥0）。
    double economy = 0.0;
    double freeArmyChance = 0.0;  // 势力8 免费产兵概率（来自定义）
    // 产兵方向：spawnAngle 是下一次产兵使用的角度；首次产兵前由 spawnAngleSet 区分未初始化。
    double spawnAngle = 0.0;
    bool spawnAngleSet = false;

    // 增益（P7）：初始 buff 来自定义（source="faction"）；mods 为聚合缓存（系统读取面）。
    std::vector<Buff> buffs;
    FactionMods mods;

    // P15：首都迁都状态机（初始首都由 initFactions 在征服初始城后设置）。
    CapitalState capitalState;

    // 兵种单价（= 定义 baseCost，P11 改版：势力 costDiv 价格折扣已移除 → 各势力同价）与
    // 累计花费（公平调度堆的排序键）。
    std::array<double, kArmyTypeCount> armyCost{};
    std::array<double, kArmyTypeCount> producedCost{};
    // P11 改版：兵种偏好系数（来自 Config::Faction.unitPreference；默认 AI 排序键
    // (已花费+单价)/偏好，>1 → 该兵种上花费更多；未来与科技挂钩）。
    std::array<double, kArmyTypeCount> unitPreference{};

    // P13：所持城市 id 列表（Map 注册表下标；注册表是唯一权威数据）。cityCount == 本列表大小。
    std::vector<int> cityIds;

    // P8：科技状态（见上 TechState）。
    TechState tech;

    // P8：科技等级 = 所有已拥有科技等级之和（排行榜"科技等级"列；每次科研 +1）。纯函数。
    int techLevel() const;
};

}  // namespace lw
