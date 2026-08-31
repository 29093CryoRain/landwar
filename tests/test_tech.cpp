// test_tech.cpp — 科技系统（开发计划 P8，思路"科技细节"）单测。
// 覆盖：tech 配置往返、科技点累积（城市等级 / 观星台）、阈值触发（AI 随机科研 / 玩家待选）、
// applyTech（升级替换 buff / mods / 偏好加成 / 行军重缩放）、choosePlayerTech（应用/跳过/防御）、
// 效果读取点（节流/经济振兴/大爆炸/高速射击/弹幕）、快照往返 + 旧档向前兼容、AI 科研确定性。
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/Buff.h"
#include "sim/systems/SpawnSystem.h"
#include "sim/systems/TechSystem.h"
#include "sim/systems/UnitActionSystem.h"
#include "world/Faction.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 精简测试用科技表（覆盖要测的类型）：0 节流·普通 / 1 行军·先锋 / 2 大爆炸 /
// 3 经济振兴 / 4 观星台 / 5 高速射击 / 6 弹幕。阈值/速率调小便于测试。
Config techCfg() {
    Config cfg = lwtest::loadCfg();  // 基础（势力/兵种/经济/地图）
    cfg.tech.techs.clear();
    cfg.tech.thresholdBase = 100.0;
    cfg.tech.thresholdStep = 100.0;
    cfg.tech.pointsPerCityLevel = 1.0;
    cfg.tech.preferencePerLevel = 0.5;
    cfg.tech.playerCandidateCount = 3;
    const auto L = [](BuffType t, int param, double mag) {
        Config::Tech::Level lv;
        lv.type = t;
        lv.param = param;
        lv.magnitude = mag;
        return lv;
    };
    auto& tn = cfg.tech.techs.emplace_back();
    tn.id = "throttle_normal"; tn.name = "节流·普通"; tn.desc = "造价";
    tn.levels = {L(BuffType::UnitCostMult, static_cast<int>(ArmyType::normal), 0.90),
                 L(BuffType::UnitCostMult, static_cast<int>(ArmyType::normal), 0.82)};
    tn.preferenceUnits = {static_cast<int>(ArmyType::normal)};

    auto& mv = cfg.tech.techs.emplace_back();
    mv.id = "march_vanguard"; mv.name = "行军·先锋"; mv.desc = "速度";
    mv.levels = {L(BuffType::UnitSpeedAdd, static_cast<int>(ArmyType::vanguard), 0.20),
                 L(BuffType::UnitSpeedAdd, static_cast<int>(ArmyType::vanguard), 0.40)};
    mv.preferenceUnits = {static_cast<int>(ArmyType::vanguard)};

    auto& bb = cfg.tech.techs.emplace_back();
    bb.id = "big_bang"; bb.name = "大爆炸"; bb.desc = "爆炸半径";
    bb.levels = {L(BuffType::ExplosionRadiusAdd, -1, 0.30)};
    bb.preferenceUnits = {static_cast<int>(ArmyType::bomb), static_cast<int>(ArmyType::mine)};

    auto& eb = cfg.tech.techs.emplace_back();
    eb.id = "economy_boost"; eb.name = "经济振兴"; eb.desc = "经济";
    eb.levels = {L(BuffType::EconomyGainAdd, -1, 0.10)};

    auto& ob = cfg.tech.techs.emplace_back();
    ob.id = "observatory"; ob.name = "观星台"; ob.desc = "科技";
    ob.levels = {L(BuffType::TechGainAdd, -1, 0.10)};

    auto& rf = cfg.tech.techs.emplace_back();
    rf.id = "rapid_fire"; rf.name = "高速射击"; rf.desc = "攻击速度";
    rf.levels = {L(BuffType::UnitActionRateAdd, static_cast<int>(ArmyType::pistol), 0.30)};
    rf.preferenceUnits = {static_cast<int>(ArmyType::pistol)};

    auto& bg = cfg.tech.techs.emplace_back();
    bg.id = "barrage"; bg.name = "弹幕"; bg.desc = "子弹数";
    bg.levels = {L(BuffType::ProjectileCountExtra, static_cast<int>(ArmyType::shotgun), 2.0)};
    bg.preferenceUnits = {static_cast<int>(ArmyType::shotgun)};
    return cfg;
}

// 注册一座归 fid 所有的城市（tech 累积用）。
int addOwnedCity(Simulation& sim, int fid, int level, int bx, int by) {
    const int cid = sim.map().addCity(level, bx, by);
    sim.map().city(cid).ownerId = fid;
    return cid;
}

// ---- 配置往返 / 数值文案 ----

TEST(Tech, ConfigRoundTrip) {
    const Config cfg = techCfg();
    const Config back = Config::loadFromJson(cfg.toJson());
    ASSERT_EQ(back.tech.techs.size(), cfg.tech.techs.size());
    EXPECT_DOUBLE_EQ(back.tech.thresholdBase, 100.0);
    EXPECT_DOUBLE_EQ(back.tech.pointsPerCityLevel, 1.0);
    EXPECT_EQ(back.tech.techIndex("throttle_normal"), 0);
    EXPECT_EQ(back.tech.techs[0].levels.size(), 2u);
    EXPECT_EQ(back.tech.techs[0].levels[0].type, BuffType::UnitCostMult);
    EXPECT_EQ(back.tech.techs[0].levels[0].param, static_cast<int>(ArmyType::normal));
    EXPECT_DOUBLE_EQ(back.tech.techs[0].levels[0].magnitude, 0.90);
    EXPECT_EQ(back.tech.techs[2].preferenceUnits.size(), 2u);  // 大爆炸：炸弹+地雷
    EXPECT_EQ(back.tech.techs[3].preferenceUnits.size(), 0u);  // 经济振兴：全局
}

TEST(Tech, BuffValueText) {
    EXPECT_EQ(buffValueText(BuffType::UnitCostMult, 0.90), "-10%");
    EXPECT_EQ(buffValueText(BuffType::UnitSpeedAdd, 0.20), "+20%");
    EXPECT_EQ(buffValueText(BuffType::ProjectileCountExtra, 2.0), "+2");
}

// 锁定真实 data/config.jsonc 的首批科技表（思路"开发时第一批做的科技"）。
// 21 个：节流×8 + 行军×6（**取消手枪/霰弹**，用户定夺 2026-08-08）+ 大爆炸/稳定激光/延长激光/
// 高速射击/弹幕/经济振兴/观星台。
TEST(Tech, RealConfigHasFirstBatchTechs) {
    const Config cfg = lwtest::loadCfg();
    ASSERT_EQ(cfg.tech.techs.size(), 21u);
    EXPECT_NE(cfg.tech.techIndex("throttle_normal"), -1);
    EXPECT_NE(cfg.tech.techIndex("march_normal"), -1);
    EXPECT_NE(cfg.tech.techIndex("march_mine"), -1);
    EXPECT_EQ(cfg.tech.techIndex("march_pistol"), -1);   // 已取消
    EXPECT_EQ(cfg.tech.techIndex("march_shotgun"), -1);  // 已取消
    EXPECT_NE(cfg.tech.techIndex("big_bang"), -1);
    EXPECT_NE(cfg.tech.techIndex("stable_laser"), -1);
    EXPECT_NE(cfg.tech.techIndex("extend_laser"), -1);
    EXPECT_NE(cfg.tech.techIndex("rapid_fire"), -1);
    EXPECT_NE(cfg.tech.techIndex("barrage"), -1);
    EXPECT_NE(cfg.tech.techIndex("economy_boost"), -1);
    EXPECT_NE(cfg.tech.techIndex("observatory"), -1);
    // 普通兵节流 4 级（四级 -30%），其余兵种 3 级。
    EXPECT_EQ(cfg.tech.techs[cfg.tech.techIndex("throttle_normal")].levels.size(), 4u);
    EXPECT_EQ(cfg.tech.techs[cfg.tech.techIndex("throttle_vanguard")].levels.size(), 3u);
    EXPECT_EQ(cfg.tech.techs[cfg.tech.techIndex("march_normal")].levels.size(), 4u);
    // 大爆炸偏好同时含地雷与炸弹；高速射击/弹幕 desc 标注兵种。
    EXPECT_EQ(cfg.tech.techs[cfg.tech.techIndex("big_bang")].preferenceUnits.size(), 2u);
    EXPECT_EQ(cfg.tech.techs[cfg.tech.techIndex("rapid_fire")].desc, "手枪兵攻击速度");
    EXPECT_EQ(cfg.tech.techs[cfg.tech.techIndex("barrage")].desc, "霰弹兵每次子弹数");
}

// ---- 科技点累积 ----

TEST(Tech, PointsAccumulateFromCities) {
    Config cfg = techCfg();
    cfg.tech.thresholdBase = 1000000.0;  // 不触发科研
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int c1 = addOwnedCity(sim, 1, 2, 10, 10);
    const int c2 = addOwnedCity(sim, 1, 4, 20, 20);
    f.cityIds = {c1, c2};
    f.cityCount = 2;
    f.tech.points = 0.0;
    TechSystem::update(sim);
    EXPECT_DOUBLE_EQ(f.tech.points, 6.0);  // 2 级 + 4 级 = 6 点 × 1.0
    EXPECT_DOUBLE_EQ(f.techRate, 6.0);
}

TEST(Tech, ObservatoryBoostsPoints) {
    Config cfg = techCfg();
    cfg.tech.thresholdBase = 1000000.0;
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int c1 = addOwnedCity(sim, 1, 2, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    f.tech.points = 0.0;
    TechSystem::applyTech(sim, 1, 4);  // 观星台 → techGainMult 1.1
    TechSystem::update(sim);
    EXPECT_DOUBLE_EQ(f.tech.points, 2.2);  // 2 级城 × 1.0 × 1.1
    EXPECT_DOUBLE_EQ(f.techRate, 2.2);
}

// ---- 阈值触发：AI 科研 / 玩家待选 ----

TEST(Tech, AIResearchesOnThreshold) {
    Config cfg = techCfg();
    cfg.tech.techs.resize(1);  // 只留节流 → AI 必选它（确定性）
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.tech.points = 100.0;  // == 阈值 → 加一城累积 +1 越过
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    TechSystem::update(sim);
    EXPECT_DOUBLE_EQ(f.tech.points, 0.0);     // 清空科技点
    EXPECT_DOUBLE_EQ(f.tech.threshold, 200.0);  // 阈值提升
    EXPECT_EQ(f.tech.levels[0], 1);
    EXPECT_EQ(f.techLevel(), 1);
    EXPECT_DOUBLE_EQ(f.mods.costMult[static_cast<size_t>(ArmyType::normal)], 0.90);
    const auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GameEventKind::TechAcquired);
    EXPECT_EQ(events[0].factionId, 1);
    EXPECT_EQ(events[0].data, 0);
}

TEST(Tech, AIResearchSkippedWhenAllMaxed) {
    Config cfg = techCfg();
    cfg.tech.techs.resize(1);
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.tech.levels[0] = 2;  // 节流已满级（maxLevel 2）
    f.tech.points = 100.0;
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    TechSystem::update(sim);
    EXPECT_DOUBLE_EQ(f.tech.points, 0.0);       // 清点
    EXPECT_DOUBLE_EQ(f.tech.threshold, 200.0);  // 阈值提升（机会浪费）
    EXPECT_EQ(f.tech.levels[0], 2);             // 不升级
    EXPECT_TRUE(sim.takeEvents().empty());      // 无事件
}

TEST(Tech, PlayerPendingSamplesCandidates) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.aiId = 1;  // 玩家势力
    f.tech.points = 100.0;  // == 阈值 → 触发
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    TechSystem::update(sim);
    EXPECT_TRUE(f.tech.researchPending);
    // 5 个可升级科技里抽 3 个（其余 -1 填充）。
    int valid = 0;
    for (int c : f.tech.candidates) if (c >= 0) ++valid;
    EXPECT_EQ(valid, 3);
    // 点/阈值未结算（选完在 choosePlayerTech 清 0 + 阈值提升）。触发前先累积 +1（本城 1 级）→ 101。
    EXPECT_DOUBLE_EQ(f.tech.points, 101.0);
    EXPECT_DOUBLE_EQ(f.tech.threshold, 100.0);
    EXPECT_EQ(f.techLevel(), 0);
}

TEST(Tech, PlayerCandidatesDeterministicSameSeed) {
    const auto run = [](std::uint32_t seed) {
        Config cfg = techCfg();
        Simulation sim(cfg, seed);
        EXPECT_TRUE(sim.init());
        auto& f = sim.faction(1);
        f.aiId = 1;
        f.tech.points = 100.0;
        const int c1 = addOwnedCity(sim, 1, 1, 10, 10);
        f.cityIds = {c1};
        f.cityCount = 1;
        TechSystem::update(sim);
        return f.tech.candidates;
    };
    const auto a = run(7);
    const auto b = run(7);
    EXPECT_EQ(a, b);
}

// ---- choosePlayerTech 结算 ----

TEST(Tech, ChoosePlayerTechApplies) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.aiId = 1;
    f.tech.researchPending = true;
    f.tech.candidates = {0, 1, 2};
    f.tech.points = 100.0;
    f.tech.threshold = 100.0;
    ASSERT_TRUE(sim.choosePlayerTech(1));  // 选行军·先锋
    EXPECT_FALSE(f.tech.researchPending);
    EXPECT_DOUBLE_EQ(f.tech.points, 0.0);
    EXPECT_DOUBLE_EQ(f.tech.threshold, 200.0);
    EXPECT_EQ(f.tech.levels[1], 1);
    EXPECT_DOUBLE_EQ(f.mods.speedMult[static_cast<size_t>(ArmyType::vanguard)], 1.20);
    const auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GameEventKind::TechAcquired);
    EXPECT_EQ(events[0].data, 1);
}

TEST(Tech, ChoosePlayerTechSkip) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.aiId = 1;
    f.tech.researchPending = true;
    f.tech.points = 100.0;
    f.tech.threshold = 100.0;
    ASSERT_TRUE(sim.choosePlayerTech(-1));  // 跳过
    EXPECT_FALSE(f.tech.researchPending);
    EXPECT_DOUBLE_EQ(f.tech.points, 0.0);
    // 2026-08 细节改进：跳过**不提升阈值**（下次升级所需科技点数不变）；等级也不变。
    EXPECT_DOUBLE_EQ(f.tech.threshold, 100.0);
    EXPECT_EQ(f.techLevel(), 0);
    EXPECT_TRUE(sim.takeEvents().empty());
}

TEST(Tech, ChoosePlayerTechRejectsInvalidCandidate) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.aiId = 1;
    f.tech.researchPending = true;
    f.tech.candidates = {0, 1, 2};
    EXPECT_FALSE(sim.choosePlayerTech(3));  // 不在候选 → 不结算
    EXPECT_TRUE(f.tech.researchPending);
}

// ---- applyTech：升级替换 / 偏好 / 行军重缩放 ----

TEST(Tech, UpgradeReplacesTechBuff) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    TechSystem::applyTech(sim, 1, 0);  // 节流 Lv1
    int count = 0;
    for (const auto& b : f.buffs) if (b.source == "tech:throttle_normal") ++count;
    EXPECT_EQ(count, 1);
    EXPECT_DOUBLE_EQ(f.mods.costMult[static_cast<size_t>(ArmyType::normal)], 0.90);
    TechSystem::applyTech(sim, 1, 0);  // 升级 Lv2 → 替换（仍 1 条，幅度 -18%）
    count = 0;
    for (const auto& b : f.buffs) if (b.source == "tech:throttle_normal") ++count;
    EXPECT_EQ(count, 1);
    EXPECT_DOUBLE_EQ(f.mods.costMult[static_cast<size_t>(ArmyType::normal)], 0.82);
    EXPECT_EQ(f.tech.levels[0], 2);
    EXPECT_EQ(f.techLevel(), 2);
}

TEST(Tech, PreferenceBonusOnApply) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const double before = f.unitPreference[static_cast<size_t>(ArmyType::normal)];
    TechSystem::applyTech(sim, 1, 0);  // 节流（pref normal）
    EXPECT_DOUBLE_EQ(f.unitPreference[static_cast<size_t>(ArmyType::normal)], before + 0.5);
    EXPECT_DOUBLE_EQ(f.unitPreference[static_cast<size_t>(ArmyType::vanguard)], 1.0);  // 不变
}

TEST(Tech, MarchRescalesExistingArmies) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    const auto e = SpawnSystem::spawnArmy(sim, 5.0, 5.0, 1, ArmyType::vanguard);
    ASSERT_TRUE(e != entt::null);  // 注：不用 ASSERT_NE（gtest 宏对 entt::null 模板实例化失败）
    const double before = sim.registry().get<comp::Speed>(e).value;
    TechSystem::applyTech(sim, 1, 1);  // 行军·先锋 Lv1 ×1.2
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Speed>(e).value, before * 1.2);
    EXPECT_DOUBLE_EQ(sim.faction(1).mods.speedMult[static_cast<size_t>(ArmyType::vanguard)], 1.20);
}

TEST(Tech, MarchDoesNotRescaleProjectiles) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    // 用 sourceType=vanguard 构造子弹（人为），确认 Projectile 标志组件被跳过（不缩放）。
    const auto e = SpawnSystem::spawnProjectile(sim, 5.0, 5.0, 1, ArmyType::vanguard, 0.0, 0.5);
    const double before = sim.registry().get<comp::Speed>(e).value;
    TechSystem::applyTech(sim, 1, 1);  // 行军·先锋
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Speed>(e).value, before);
}

// ---- 效果读取点 ----

TEST(Tech, EconomyBoostAndBigBangMods) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    TechSystem::applyTech(sim, 1, 3);  // 经济振兴 → economyGainMult 1.1
    EXPECT_DOUBLE_EQ(sim.faction(1).mods.economyGainMult, 1.10);
    TechSystem::applyTech(sim, 1, 2);  // 大爆炸 → 通用爆炸效果半径增幅 1.3
    EXPECT_DOUBLE_EQ(sim.faction(1).mods.explosionRadiusAdd, 1.30);
}

TEST(Tech, RapidFireAndBarrageMods) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    TechSystem::applyTech(sim, 1, 5);  // 高速射击
    EXPECT_DOUBLE_EQ(
        sim.faction(1).mods.actionRateMult[static_cast<size_t>(ArmyType::pistol)], 1.30);
    TechSystem::applyTech(sim, 1, 6);  // 弹幕
    EXPECT_EQ(
        sim.faction(1).mods.projectileCountExtra[static_cast<size_t>(ArmyType::shotgun)], 2);
}

TEST(Tech, RapidFireFiresFaster) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    SpawnSystem::spawnArmy(sim, 5.0, 5.0, 1, ArmyType::pistol);  // periodTicks=240
    TechSystem::applyTech(sim, 1, 5);  // 射速 ×1.3 → 有效周期 = round(240/1.3)=185
    // 跑 200 次：185 时必已触发；无科技则 240 才触发。
    for (int i = 0; i < 200; ++i) UnitActionSystem::update(sim);
    int projectiles = 0;
    for ([[maybe_unused]] auto e : sim.registry().view<comp::Projectile>()) ++projectiles;
    EXPECT_GE(projectiles, 1);
}

TEST(Tech, BarrageFiresMoreBullets) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    SpawnSystem::spawnArmy(sim, 5.0, 5.0, 1, ArmyType::shotgun);  // periodTicks=360, base 3 发
    TechSystem::applyTech(sim, 1, 6);  // 弹幕 +2 → 5 发
    for (int i = 0; i < 360; ++i) UnitActionSystem::update(sim);
    int projectiles = 0;
    for ([[maybe_unused]] auto e : sim.registry().view<comp::Projectile>()) ++projectiles;
    EXPECT_EQ(projectiles, 5);
}

// ---- 快照 ----

TEST(Tech, SnapshotRoundTripPreservesTechAndBuffs) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.aiId = 1;
    f.tech.points = 50.0;
    f.tech.threshold = 100.0;
    TechSystem::applyTech(sim, 1, 0);  // 节流 Lv1
    f.tech.candidates = {0, 1, 2};
    const std::string json = Snapshot::serialize(sim);
    Simulation loaded;
    ASSERT_TRUE(Snapshot::deserialize(loaded, json));
    auto& lf = loaded.faction(1);
    EXPECT_DOUBLE_EQ(lf.tech.points, 50.0);
    EXPECT_DOUBLE_EQ(lf.tech.threshold, 100.0);
    EXPECT_EQ(lf.tech.levels[0], 1);
    EXPECT_EQ(lf.tech.candidates.size(), 3u);
    EXPECT_EQ(lf.tech.candidates[0], f.tech.candidates[0]);
    // 读档后 buffs 从 def + tech.levels 重建 → mods 恢复（节流生效）。
    EXPECT_DOUBLE_EQ(lf.mods.costMult[static_cast<size_t>(ArmyType::normal)], 0.90);
    EXPECT_EQ(lf.techLevel(), 1);
}

TEST(Tech, SnapshotWithoutTechIsRejected) {
    Config cfg = techCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto json = nlohmann::json::parse(Snapshot::serialize(sim));
    json["config"].erase("tech");  // 模拟旧版存档（无 tech 配置）
    for (auto& fj : json["factions"]) fj.erase("tech");
    Simulation loaded;
    std::string err;
    EXPECT_FALSE(Snapshot::deserialize(loaded, json.dump(), &err));
    EXPECT_FALSE(err.empty());
}

// ---- 确定性：AI 科研同种子逐级一致 ----

TEST(Tech, AIResearchDeterministicSameSeed) {
    const auto run = [](std::uint32_t seed) {
        Config cfg = techCfg();
        cfg.tech.thresholdBase = 50.0;  // 快点触发（约 50 tick 一次）
        Simulation sim(cfg, seed);
        EXPECT_TRUE(sim.init());
        for (int i = 0; i < 200; ++i) sim.tick();
        return sim.faction(1).tech.levels;
    };
    EXPECT_EQ(run(9), run(9));
}

}  // namespace
