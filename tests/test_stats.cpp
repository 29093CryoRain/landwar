// test_stats.cpp — 统计系统单测（开发计划 P11，思路 8）。
// 覆盖：record* 与汇总、截止冻结、recompute 重建、credit 链（产兵/战斗互杀/爆炸特效/激光特效/
// 子弹击杀/移动占领）、快照往返。
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/systems/CombatSystem.h"
#include "sim/systems/MovementSystem.h"
#include "sim/systems/SpawnSystem.h"
#include "TestUtil.h"

namespace {

using namespace lw;

Config baseCfg() { return lwtest::loadCfg(); }

// 清空全部兵实体（保留特效），得到干净测试场。
void clearArmies(Simulation& sim) {
    auto& reg = sim.registry();
    std::vector<entt::entity> all;
    for (auto e : reg.view<comp::Position, comp::Collider>()) all.push_back(e);
    for (auto e : all) reg.destroy(e);
}

// 把地图整片铺成陆地 + 指定归属（credit 测试需可控地形）。
void clearToLand(Simulation& sim, int owner) {
    auto& map = sim.map();
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x) {
            auto& c = map.at(x, y);
            c.land = true;
            c.cityId = -1;
            c.belongi = owner;
        }
}

// ---- 数据模型 ----

TEST(Stats, RecordMethodsUpdateCounts) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.stats().recordKill(1, static_cast<int>(ArmyType::normal));
    sim.stats().recordKill(1, static_cast<int>(ArmyType::normal));
    sim.stats().recordLand(3, static_cast<int>(ArmyType::pioneer));
    sim.stats().recordProduced(5, static_cast<int>(ArmyType::laser));
    // (势力,兵种)
    EXPECT_EQ(sim.stats().perFactionUnit[1][0].kills, 2);
    EXPECT_EQ(sim.stats().perFactionUnit[3][2].lands, 1);
    EXPECT_EQ(sim.stats().perFactionUnit[5][3].produced, 1);
    // 汇总
    EXPECT_EQ(sim.stats().factionKills[1], 2);
    EXPECT_EQ(sim.stats().factionLands[3], 1);
    EXPECT_EQ(sim.stats().factionProduced[5], 1);
    EXPECT_EQ(sim.stats().unitKills[0], 2);
    EXPECT_EQ(sim.stats().unitLands[2], 1);
    EXPECT_EQ(sim.stats().unitProduced[3], 1);
    // 中立/越界忽略。
    sim.stats().recordKill(0, 0);
    sim.stats().recordKill(1, -1);
    sim.stats().recordKill(1, kArmyTypeCount);
    EXPECT_EQ(sim.stats().factionKills[1], 2);  // 未增
}

TEST(Stats, CutoffFreezesRecords) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.stats().cutoffTick = 100;  // 统一发生
    sim.stats().recordKill(1, 0);
    sim.stats().recordLand(1, 0);
    sim.stats().recordProduced(1, 0);
    EXPECT_EQ(sim.stats().factionKills[1], 0);
    EXPECT_EQ(sim.stats().factionLands[1], 0);
    EXPECT_EQ(sim.stats().factionProduced[1], 0);
}

TEST(Stats, RecomputeRebuildsSummaries) {
    Statistics s;
    s.perFactionUnit[1][0].kills = 3;
    s.perFactionUnit[2][3].lands = 5;
    s.perFactionUnit[1][3].produced = 7;
    s.recompute();
    EXPECT_EQ(s.factionKills[1], 3);
    EXPECT_EQ(s.factionLands[2], 5);
    EXPECT_EQ(s.factionProduced[1], 7);
    EXPECT_EQ(s.unitKills[0], 3);
    EXPECT_EQ(s.unitLands[3], 5);
    EXPECT_EQ(s.unitProduced[3], 7);
}

// ---- credit 链 ----

TEST(Stats, ProducedRecordedOnSpawn) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    SpawnSystem::spawnArmy(sim, 5.5, 5.5, 1, ArmyType::normal);
    SpawnSystem::spawnArmy(sim, 6.5, 6.5, 3, ArmyType::laser);
    EXPECT_EQ(sim.stats().perFactionUnit[1][0].produced, 1);  // 势力1 普通
    EXPECT_EQ(sim.stats().perFactionUnit[3][3].produced, 1);  // 势力3 激光
}

TEST(Stats, CombatMutualKillCredited) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    // 两支敌对兵相邻（距离 < 半径和 → 近战触发）。
    SpawnSystem::spawnArmy(sim, 10.5, 10.5, 1, ArmyType::normal);
    SpawnSystem::spawnArmy(sim, 10.8, 10.5, 2, ArmyType::vanguard);
    auto ctx = MovementSystem::makeContext(sim);
    ctx.spatialHash.build(ctx.registry, ctx.map.width(), ctx.map.height());
    std::vector<entt::entity> armies;
    for (auto e : ctx.registry.view<comp::UnitType>()) armies.push_back(e);
    ASSERT_EQ(armies.size(), 2u);
    const auto& p = ctx.registry.get<comp::Position>(armies[0]);
    const auto& c = ctx.registry.get<comp::Collider>(armies[0]);
    const auto& f = ctx.registry.get<comp::FactionId>(armies[0]);
    CombatSystem::checkAt(ctx, armies[0], p, c, f);
    // 双方互杀，击杀 credit = 击杀者：势力1 普通杀 势力2 先锋 → (1, normal)；
    // 势力2 先锋杀 势力1 普通 → (2, vanguard)。
    EXPECT_EQ(sim.stats().perFactionUnit[1][static_cast<int>(ArmyType::normal)].kills, 1);
    EXPECT_EQ(sim.stats().perFactionUnit[2][static_cast<int>(ArmyType::vanguard)].kills, 1);
}

TEST(Stats, BombEffectCreditsCreator) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);  // 全中立陆地
    clearArmies(sim);
    SpawnSystem::spawnArmy(sim, 21.5, 20.5, 2, ArmyType::normal);  // 敌兵在爆炸半径内
    // 爆炸特效：创建者 = 势力1 爆炸兵（unitType=bomb）。
    SpawnSystem::spawnEffect(sim, 20.5, 20.5, 1, EffectType::bomb, 2.4,
                             comp::Creator{20.5, 20.5, 1, static_cast<int>(ArmyType::bomb)});
    sim.tick();
    // 爆炸占地 + 击杀敌兵 → credit (1, bomb)。
    EXPECT_GT(sim.stats().perFactionUnit[1][static_cast<int>(ArmyType::bomb)].lands, 0);
    EXPECT_EQ(sim.stats().perFactionUnit[1][static_cast<int>(ArmyType::bomb)].kills, 1);
}

TEST(Stats, LaserEffectCreditsCreator) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 1);  // 全势力1陆地（激光在己方陆地延伸，尖端无占领）
    clearArmies(sim);
    SpawnSystem::spawnArmy(sim, 25.5, 20.5, 2, ArmyType::normal);  // 敌兵在光束路径上
    SpawnSystem::spawnEffect(sim, 20.5, 20.5, 1, EffectType::laser, 0.0 /*angle=0 → +x*/,
                             comp::Creator{20.5, 20.5, 1, static_cast<int>(ArmyType::laser)});
    sim.tick();
    EXPECT_EQ(sim.stats().perFactionUnit[1][static_cast<int>(ArmyType::laser)].kills, 1);
}

TEST(Stats, ProjectileKillCreditsFirer) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    SpawnSystem::spawnArmy(sim, 12.5, 10.5, 2, ArmyType::normal);  // 敌兵距子弹出生点 2.0
    // 子弹：势力1 手枪（sourceType=pistol），angle=0，speed 5。
    SpawnSystem::spawnProjectile(sim, 10.5, 10.5, 1, ArmyType::pistol, 0.0, 5.0);
    sim.tick();
    EXPECT_EQ(sim.stats().perFactionUnit[1][static_cast<int>(ArmyType::pistol)].kills, 1);
}

TEST(Stats, MovementConquestCreditsUnit) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);  // 全中立陆地
    clearArmies(sim);
    SpawnSystem::spawnArmy(sim, 5.5, 5.5, 1, ArmyType::pioneer);
    for (int i = 0; i < 30; ++i) sim.tick();
    // 开拓兵移动征服中立陆地 → land credit (1, pioneer)。
    EXPECT_GT(sim.stats().perFactionUnit[1][static_cast<int>(ArmyType::pioneer)].lands, 0);
}

// ---- 快照往返 ----

TEST(Stats, SnapshotRoundTrip) {
    Simulation a(baseCfg(), 42);
    ASSERT_TRUE(a.init());
    a.stats().recordKill(1, 0);
    a.stats().recordLand(3, 2);
    a.stats().recordProduced(5, 6);
    a.stats().cutoffTick = 1234;
    const std::string json = Snapshot::serialize(a);
    Simulation b;
    ASSERT_TRUE(Snapshot::deserialize(b, json));
    EXPECT_EQ(b.stats().perFactionUnit[1][0].kills, 1);
    EXPECT_EQ(b.stats().perFactionUnit[3][2].lands, 1);
    EXPECT_EQ(b.stats().perFactionUnit[5][6].produced, 1);
    EXPECT_EQ(b.stats().cutoffTick, 1234u);
    EXPECT_EQ(b.stats().factionKills[1], 1);  // 汇总读档重建
    EXPECT_EQ(b.stats().unitKills[0], 1);
}

}  // namespace
