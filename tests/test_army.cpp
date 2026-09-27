// test_army.cpp — 军队系统单测（翻新计划 §2.2-§2.5 / Phase 3）。
// 移动分支用 MockRng 固定 chance 结果 + 精确位置，确定性验证下海/登陆/反弹/征服/战斗/死亡效果。
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/Simulation.h"
#include "sim/ConquestRules.h"
#include "sim/SpatialHash.h"
#include "sim/components.h"
#include "sim/systems/CombatSystem.h"
#include "sim/systems/DeathSystem.h"
#include "sim/systems/MovementSystem.h"
#include "sim/systems/SpawnSystem.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 全陆小地图 + 势力 + 可控 rng，供 moveArmy 分支单测（绕过 Simulation::tick）。
struct TestWorld {
    Config cfg;
    Map map;
    lwtest::MockRng rng;
    std::vector<Faction> factions;
    std::vector<PendingSpawn> pending;
    std::vector<DeathEvent> deaths;
    entt::registry reg;
    SpatialHash hash;
    double goSeaProb = 1.0;
    int ttime = 0;

    explicit TestWorld(int width = 10, int height = 10) : cfg(lwtest::loadCfg()) {
        cfg.map.width = width;
        cfg.map.height = height;
        map.configure(cfg.map);
        factions.resize(static_cast<size_t>(kFactionTotal));
        for (int id = 0; id < kFactionTotal; ++id)
            factions[static_cast<size_t>(id)].initFromDef(cfg.factions[static_cast<size_t>(id)], cfg);
    }
};

MoveContext makeCtx(TestWorld& w) {
    return MoveContext{w.map,     w.factions, w.rng,   w.pending, w.deaths,
                       w.reg,     w.hash,     w.goSeaProb, w.ttime, w.cfg};
}

entt::entity addArmy(TestWorld& w, double x, double y, int fid, ArmyType type,
                     double angle, double speed, bool onland, bool inMountain = false) {
    auto e = w.reg.create();
    w.reg.emplace<comp::Position>(e, x, y);
    w.reg.emplace<comp::Velocity>(e, angle);
    w.reg.emplace<comp::Speed>(e, speed);
    w.reg.emplace<comp::OnLand>(e, onland);
    w.reg.emplace<comp::MountainState>(e, inMountain);
    w.reg.emplace<comp::FactionId>(e, fid);
    w.reg.emplace<comp::UnitType>(e, type);
    w.reg.emplace<comp::Collider>(e, 1.1);
    w.reg.emplace<comp::LandHistory>(e, 0);
    // 与 SpawnSystem::spawnArmy 对齐：兵必带闸门统计量（0 = 未受压）。
    w.reg.emplace<comp::AllyGate>(e);
    return e;
}

void moveOnce(TestWorld& w, entt::entity e) {
    auto ctx = makeCtx(w);
    MovementSystem::moveArmy(ctx, e);
}

// 把 w 布置成"己方 8×5=40 格地块被盟友领土完全包围"（须用 TestWorld(16,14) 构造）；
// 闸门端到端测试共用同一场景。
void setupEnclosedFortyCellPocket(TestWorld& w) {
    constexpr int kCols = 16, kRows = 14;
    constexpr int kOwnX0 = 4, kOwnY0 = 4, kOwnW = 8, kOwnH = 5;
    for (int y = 0; y < kRows; ++y)
        for (int x = 0; x < kCols; ++x) {
            lwtest::atXY(w.map, x, y).land = true;
            lwtest::atXY(w.map, x, y).belongi = 2;  // 全图盟友领土：把己方地块四面完全包住
        }
    for (int y = kOwnY0; y < kOwnY0 + kOwnH; ++y)
        for (int x = kOwnX0; x < kOwnX0 + kOwnW; ++x) lwtest::atXY(w.map, x, y).belongi = 1;
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    w.factions[1].landCount = kOwnW * kOwnH;
    w.factions[2].landCount = kCols * kRows - kOwnW * kOwnH;
}

// 从当前位置起推进，返回兵离开己方地块（进入盟友领土）的 tick；maxTicks 内没离开 → -1。
// alarmed（可选）记录期间闸门统计量是否曾达阈值。
int tickUntilLeavesOwnLand(TestWorld& w, entt::entity e, int maxTicks, bool* alarmed = nullptr) {
    for (int i = 0; i < maxTicks; ++i) {
        moveOnce(w, e);
        if (alarmed && w.reg.get<comp::AllyGate>(e).cusum >= w.cfg.allyGate.alarmThreshold)
            *alarmed = true;
        const auto& p = w.reg.get<comp::Position>(e);
        const int cell = w.map.geom().worldToCell(p.x, p.y);
        if (cell >= 0 && w.map.atIndex(cell).belongi != 1) return i + 1;
    }
    return -1;
}

// ---- Spawn ----

TEST(Spawn, ArmyFieldsCorrect) {
    const Config cfg = lwtest::loadCfg();
    MapDefinition definition;
    definition.cols = cfg.map.width;
    definition.rows = cfg.map.height;
    definition.tiling = cfg.map.tilingType();
    definition.terrain.assign(static_cast<std::size_t>(definition.cols * definition.rows),
                               MapTerrain::Land);
    Simulation sim(cfg, 42);
    sim.setMapDefinition(std::move(definition));
    ASSERT_TRUE(sim.init());
    auto e = SpawnSystem::spawnArmy(sim, 5.0, 5.0, 1, ArmyType::normal);
    ASSERT_TRUE(e != entt::null);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Position>(e).x, 5.0);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Position>(e).y, 5.0);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Speed>(e).value, 0.15);
    // 半径 = config.army.baseSize × 兵种 sizeMult（随 config 变化，勿硬编码）。
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Collider>(e).radius, sim.config().army.baseSize);
    EXPECT_TRUE(sim.registry().get<comp::OnLand>(e).value);
    EXPECT_EQ(sim.registry().get<comp::UnitType>(e).type, ArmyType::normal);
    EXPECT_EQ(sim.registry().get<comp::FactionId>(e).value, 1);
    EXPECT_EQ(sim.registry().get<comp::LandHistory>(e).lastLandTime, 0);
}

TEST(Spawn, SpeedAndSizeChains) {
    struct Case {
        int fid;
        ArmyType type;
        double speed;
        double size;
    };
    // 半径期望 = baseSize × 兵种 sizeMult（随 config 变化，勿硬编码）。
    const Config loadedCfg = lwtest::loadCfg();
    const double base = loadedCfg.army.baseSize;
    const double speed = loadedCfg.army.baseSpeed;
    const std::vector<Case> cases = {
        {1, ArmyType::normal, speed, base},
        {3, ArmyType::vanguard, speed * loadedCfg.factions[3].speedMultAll * 2.0, base},
        {3, ArmyType::laser, speed * loadedCfg.factions[3].speedMultAll * 0.6, base * 1.8},
        {4, ArmyType::pioneer, speed * loadedCfg.factions[4].pioneerSpeedMult, base},
        {5, ArmyType::laser, speed * 0.6, base * 1.8},     // 绿无全速 ×1.5
        {1, ArmyType::bomb, speed * 0.6, base * 1.8},
        {1, ArmyType::mine, speed * 0.6, base * 1.4},
    };
    Simulation sim(lwtest::loadCfg(), 7);
    ASSERT_TRUE(sim.init());
    for (const auto& c : cases) {
        auto e = SpawnSystem::spawnArmy(sim, 10.0, 10.0, c.fid, c.type);
        ASSERT_TRUE(e != entt::null);
        EXPECT_DOUBLE_EQ(sim.registry().get<comp::Speed>(e).value, c.speed)
            << "fid " << c.fid << " type " << static_cast<int>(c.type);
        EXPECT_DOUBLE_EQ(sim.registry().get<comp::Collider>(e).radius, c.size)
            << "fid " << c.fid << " type " << static_cast<int>(c.type);
    }
}

TEST(Spawn, AngleUsesFactionSequence) {
    // Phase 3.3：每个势力首次产兵随机，之后使用上一角度 + spawnAngleStep，且不再消耗 RNG。
    Simulation sim(lwtest::loadCfg(), 99);
    ASSERT_TRUE(sim.init());
    ASSERT_TRUE(sim.faction(1).spawnAngleSet);  // 开局已确定，UI 无需等待首次产兵。
    const double initialAngle = sim.faction(1).spawnAngle;
    auto first = SpawnSystem::spawnArmy(sim, 20.0, 20.0, 1, ArmyType::normal);
    ASSERT_TRUE(first != entt::null);
    const double firstAngle = sim.registry().get<comp::Velocity>(first).angle;
    EXPECT_DOUBLE_EQ(firstAngle, initialAngle);
    const auto rngAfterFirst = sim.rng().state();
    auto second = SpawnSystem::spawnArmy(sim, 20.0, 20.0, 1, ArmyType::normal);
    ASSERT_TRUE(second != entt::null);
    const double expected = std::fmod(firstAngle + sim.config().army.spawnAngleStep, 2 * kPi);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Velocity>(second).angle, expected);
    EXPECT_EQ(sim.rng().state(), rngAfterFirst);
    EXPECT_DOUBLE_EQ(sim.faction(1).spawnAngle,
                     std::fmod(expected + sim.config().army.spawnAngleStep, 2 * kPi));
    EXPECT_TRUE(sim.faction(1).spawnAngleSet);
}

// ---- Movement 分支（用精确位置 1.9 保证单 tick 恰好跨格）----

TEST(Movement, GoToSeaSucceeds) {
    TestWorld w(5, 5);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    // (2,1) 默认海。
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {true};  // 下海成功
    moveOnce(w, e);
    EXPECT_FALSE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.15);  // speed /2
    // 2026-08 用户定夺：下海不再 break → 剩余步长(0.2)减半(0.1)后继续入海，x 越过边界到 2.1
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 2.1, 1e-9);
}

TEST(Movement, GoToSeaBouncesWhenChanceFails) {
    TestWorld w(5, 5);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {false};  // 下海失败 → 水平反弹
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹角 + 随机小偏置
}

TEST(Movement, BounceJitterUsesConfiguredUnitRange) {
    TestWorld w(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            lwtest::atXY(w.map, x, y).land = true;
            lwtest::atXY(w.map, x, y).belongi = 1;
        }
    auto e = addArmy(w, 0.2, 2.5, 1, ArmyType::normal, kPi, 0.3, true);
    w.rng.units = {0.0};  // 反弹偏置取下界 -rangeRad。
    moveOnce(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle,
                -w.cfg.army.bounceJitterRangeRad, 1e-12);
    EXPECT_EQ(w.rng.nextUnit, 1u);
}

TEST(Movement, LandingFromSeaRestoresSpeed) {
    TestWorld w(5, 5);
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 0;  // 中立陆
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, false);
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);  // speed ×2 恢复
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 1);                    // 登陆征服
    EXPECT_EQ(w.factions[1].landCount, 1);
}

TEST(Movement, ConquersEnemyLandAndBounces) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {true};  // 反弹
    moveOnce(w, e);
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 1);  // 攻占成功
    EXPECT_EQ(w.factions[1].landCount, 1);
    EXPECT_EQ(w.factions[2].landCount, 0);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹角 + 随机小偏置
}

TEST(Movement, ConquersEnemyLandVanguardDoesNotBounce) {
    // 细节改进：先锋兵碰敌方领土概率不反弹（bounceMult=0.4）→ 征服后继续前进。
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::vanguard, 0.0, 0.3, true);
    w.rng.results = {false};  // 敌方反弹不中 → 不反弹
    moveOnce(w, e);
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 1);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 1e-9);
}

TEST(Movement, VanguardBouncesOnEnemy) {
    // 先锋也可能反弹（bounceMult=0.4 中签）：先征服再反弹（原版"弹回"语义）。
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::vanguard, 0.0, 0.3, true);
    w.rng.results = {true};  // 敌方阻挡命中 → 反弹
    moveOnce(w, e);
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 1);  // 征服已发生
    EXPECT_EQ(w.factions[1].landCount, 1);
    EXPECT_EQ(w.factions[2].landCount, 0);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹角 + 随机小偏置
}

TEST(Movement, PioneerConquersAdjacentCellsOnEnemy) {
    // 细节改进：开拓兵碰敌方领土时，上下左右 4 邻格也一起占领（正方形网格）。
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;   // 目标格：敌方
    lwtest::atXY(w.map, 2, 1).belongi = 2;
    lwtest::atXY(w.map, 2, 2).land = true;   // 上
    lwtest::atXY(w.map, 2, 2).belongi = 2;
    lwtest::atXY(w.map, 2, 0).land = true;   // 下
    lwtest::atXY(w.map, 2, 0).belongi = 2;
    lwtest::atXY(w.map, 3, 1).land = true;   // 右
    lwtest::atXY(w.map, 3, 1).belongi = 2;
    w.factions[1].landCount = 1;  // 原点 (1,1)
    w.factions[2].landCount = 4;  // 目标+上+下+右
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::pioneer, 0.0, 0.3, true);
    w.rng.results = {false};  // 敌方反弹不中（开拓 bounceMult=1.0 实际必反弹，此处测试连占路径）
    moveOnce(w, e);
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 1);  // 目标格
    EXPECT_EQ(lwtest::atXY(w.map, 2, 2).belongi, 1);  // 上
    EXPECT_EQ(lwtest::atXY(w.map, 2, 0).belongi, 1);  // 下
    EXPECT_EQ(lwtest::atXY(w.map, 3, 1).belongi, 1);  // 右
    EXPECT_EQ(lwtest::atXY(w.map, 1, 1).belongi, 1);  // 左（原点，同势力不变）
    EXPECT_EQ(w.factions[1].landCount, 5);  // 原点 1 + 新占 4
    EXPECT_EQ(w.factions[2].landCount, 0);
}

// ---- 兵运动规范：不可占领格（盟友 / 禁征服）与海洋闸门（开发文档 §4）----

TEST(ConquestRules, CanConquerCellTruthTable) {
    TestWorld w(5, 5);
    lwtest::atXY(w.map, 1, 1).land = true;  // 己方
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;  // 中立
    lwtest::atXY(w.map, 2, 1).belongi = 0;
    lwtest::atXY(w.map, 3, 1).land = true;  // 敌方
    lwtest::atXY(w.map, 3, 1).belongi = 2;
    const int own = lwtest::cellIndex(w.map, 1, 1);
    const int neutral = lwtest::cellIndex(w.map, 2, 1);
    const int enemy = lwtest::cellIndex(w.map, 3, 1);
    const int sea = lwtest::cellIndex(w.map, 4, 1);  // 默认海
    const int normal = static_cast<int>(ArmyType::normal);

    EXPECT_TRUE(canConquerCell(w.map, w.factions, 1, normal, own));
    EXPECT_TRUE(canConquerCell(w.map, w.factions, 1, normal, neutral));
    EXPECT_TRUE(canConquerCell(w.map, w.factions, 1, normal, enemy));
    EXPECT_FALSE(canConquerCell(w.map, w.factions, 1, normal, sea));
    EXPECT_FALSE(canConquerCell(w.map, w.factions, 1, normal, -1));
    EXPECT_FALSE(canConquerCell(w.map, w.factions, 1, normal, w.map.cellCount()));

    // 盟友：敌方格变为不可占领，"友敌"判定同时翻转。
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    EXPECT_TRUE(areAllied(w.factions, 1, 2));
    EXPECT_FALSE(areEnemies(w.factions, 1, 2));
    EXPECT_FALSE(canConquerCell(w.map, w.factions, 1, normal, enemy));
    EXPECT_TRUE(areEnemies(w.factions, 1, 3));  // 无联盟者仍是敌

    // 禁征服（密集防御机制字段）：只限该兵种，且只限敌方陆；中立仍可占。
    w.factions[1].allianceId = -1;
    w.factions[2].allianceId = -1;
    w.factions[1].mods.noEnemyConquer[static_cast<size_t>(ArmyType::normal)] = true;
    EXPECT_FALSE(canConquerCell(w.map, w.factions, 1, normal, enemy));
    EXPECT_TRUE(canConquerCell(w.map, w.factions, 1, normal, neutral));
    EXPECT_TRUE(canConquerCell(w.map, w.factions, 1, static_cast<int>(ArmyType::vanguard), enemy));
    EXPECT_TRUE(canConquerCell(w.map, w.factions, 1, -1, enemy));  // 无兵种来源不受限
}

TEST(Movement, AllyLandBouncesWithoutConquering) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    w.factions[1].landCount = 1;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    moveOnce(w, e);  // 规则 2：确定反弹、不占领，且不掷 bounceMult
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 2);
    EXPECT_EQ(w.factions[1].landCount, 1);
    EXPECT_EQ(w.factions[2].landCount, 1);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);
    EXPECT_EQ(w.rng.next, 0u);  // 未消耗 chance
}

TEST(Movement, PassesThroughAllyLandWithoutBounce) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 2;
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    moveOnce(w, e);  // 规则 1：两岸都不可占领 → 直行
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 1e-9);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 2.2, 1e-9);
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 2);
}

TEST(Movement, FromAllyLandConquersWithoutBounce) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 2;  // 原格：盟友
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 3;  // 目标格：敌方
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    w.factions[2].landCount = 1;
    w.factions[3].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    moveOnce(w, e);  // 规则 3：占领且不掷反弹
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 1);
    EXPECT_EQ(w.factions[3].landCount, 0);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 1e-9);
    EXPECT_EQ(w.rng.next, 0u);
}

TEST(Movement, LandsOnAllyLandWithoutConquering) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;  // 盟友陆，四周为海
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, false);
    moveOnce(w, e);  // 规则 1：登陆但不征服、不反弹
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);  // 复原陆速
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 2);
    EXPECT_EQ(w.factions[2].landCount, 1);
}

TEST(Movement, AllyLandToSeaStillUsesGoSeaGate) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 2;  // 盟友陆；(2,1) 为海
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {false};  // 海洋闸门优先：失败仍反弹（不因两岸不可占领而必然入海）
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);
    EXPECT_EQ(w.rng.next, 1u);
}

TEST(Movement, AllyLandToSeaEntersOnGoSeaSuccess) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 2;
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {true};  // 下海成功
    moveOnce(w, e);
    EXPECT_FALSE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.15);
}

// ---- 盟友边界闸门（CUSUM 检测被盟友领土围死；开发文档 §12）----

// 盟友陆地判定与 canConquerCell 共用同一份盟友关系（唯一入口）。
TEST(ConquestRules, IsAlliedLandTruthTable) {
    TestWorld w(5, 5);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;  // 己方
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;  // 盟友（下面建盟）
    lwtest::atXY(w.map, 3, 1).land = true;
    lwtest::atXY(w.map, 3, 1).belongi = 3;  // 敌方
    lwtest::atXY(w.map, 4, 1).belongi = 0;  // 中立（默认海 + 归属 0）
    const int own = lwtest::cellIndex(w.map, 1, 1);
    const int allied = lwtest::cellIndex(w.map, 2, 1);
    const int enemy = lwtest::cellIndex(w.map, 3, 1);
    const int neutral = lwtest::cellIndex(w.map, 4, 1);
    const int sea = lwtest::cellIndex(w.map, 0, 0);  // 默认海

    EXPECT_FALSE(isAlliedLand(w.map, w.factions, 1, own));
    EXPECT_FALSE(isAlliedLand(w.map, w.factions, 1, enemy));
    EXPECT_FALSE(isAlliedLand(w.map, w.factions, 1, sea));
    EXPECT_FALSE(isAlliedLand(w.map, w.factions, 1, -1));
    EXPECT_FALSE(isAlliedLand(w.map, w.factions, 1, w.map.cellCount()));
    // 中立陆也不是盟友。
    lwtest::atXY(w.map, 4, 1).land = true;
    EXPECT_FALSE(isAlliedLand(w.map, w.factions, 1, neutral));

    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    EXPECT_TRUE(isAlliedLand(w.map, w.factions, 1, allied));
    EXPECT_FALSE(isAlliedLand(w.map, w.factions, 1, own));
}

// 被困单位反复撞盟友边界 → 统计量累积；越过报警阈值后下一次穿越放行一次（不反弹、不征服）并清零。
TEST(Movement, AllyGateReleasesTrappedArmyThroughAllyLand) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;  // 己方小飞地
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;  // 盟友（围住飞地的一侧）
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    w.factions[1].landCount = 1;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true);

    const double h = w.cfg.allyGate.alarmThreshold;
    const int ownIdx = lwtest::cellIndex(w.map, 1, 1);
    const int allyIdx = lwtest::cellIndex(w.map, 2, 1);
    int bounces = 0;
    bool released = false;
    for (int i = 0; i < 50 && !released; ++i) {
        // 把兵放回飞地、朝盟友边冲：模拟被困单位的反复尝试（否则一次反弹后就飞走了）。
        w.reg.get<comp::Position>(e) = comp::Position{1.9, 1.9};
        w.reg.get<comp::Velocity>(e).angle = 0.0;
        moveOnce(w, e);
        const auto& p = w.reg.get<comp::Position>(e);
        const int cell = w.map.geom().worldToCell(p.x, p.y);
        const auto& gate = w.reg.get<comp::AllyGate>(e);
        if (cell == allyIdx) {
            released = true;
            EXPECT_DOUBLE_EQ(gate.cusum, 0.0);  // 放行后清零
        } else {
            EXPECT_EQ(cell, ownIdx);
            ++bounces;
            EXPECT_GT(gate.cusum, 0.0);  // 盟友边界反弹喂养闸门
        }
    }
    EXPECT_TRUE(released);
    EXPECT_GE(bounces, static_cast<int>(h));  // 至少 h 次边界反弹才够报警证据
    EXPECT_LT(bounces, static_cast<int>(h) + 10);  // h 有限 → 不会无限反弹
    EXPECT_EQ(lwtest::atXY(w.map, 2, 1).belongi, 2);   // 放行不征服
    EXPECT_EQ(w.factions[2].landCount, 1);
}

// 泄漏项乘本 tick 步长（= 该兵速度）：同一次盟友边界反弹、同一初值，步长越大统计量越低
// → 判据按"每格尝试次数"计，与兵速无关。
TEST(Movement, AllyGateLeakScalesWithArmySpeed) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true);
    const double leakPerCell =
        w.cfg.allyGate.leakPerCell + w.cfg.allyGate.tolerancePerCell;

    // 从同一非零初值出发（避开 0 处的 max 反射）：慢速一次反弹。
    w.reg.get<comp::AllyGate>(e).cusum = w.cfg.allyGate.alarmThreshold * 0.25;
    moveOnce(w, e);
    const double slow = w.reg.get<comp::AllyGate>(e).cusum;

    // 快速一次反弹：本 tick 步长 0.15 → 0.3，泄漏多 (c+η)×0.15。
    w.reg.get<comp::Position>(e) = comp::Position{1.9, 1.9};
    w.reg.get<comp::Velocity>(e).angle = 0.0;
    w.reg.get<comp::AllyGate>(e).cusum = w.cfg.allyGate.alarmThreshold * 0.25;
    w.reg.get<comp::Speed>(e).value = 0.3;
    moveOnce(w, e);
    const double fast = w.reg.get<comp::AllyGate>(e).cusum;

    EXPECT_NEAR(slow - fast, leakPerCell * 0.15, 1e-9);
    EXPECT_GT(slow, fast);
}

// 直接穿过盟友领土（规则 1/3）不计观测；在盟友领土内部的反弹（河/山骰）也不算边界尝试。
TEST(Movement, AllyGateIgnoresNonBorderCrossings) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 2;  // 原格：盟友内部
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 2;  // 目标格：盟友内部
    w.factions[1].allianceId = 0;
    w.factions[2].allianceId = 0;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true);
    moveOnce(w, e);  // 规则 1：直行穿过，无反弹
    EXPECT_DOUBLE_EQ(w.reg.get<comp::AllyGate>(e).cusum, 0.0);

    // 原格也是盟友陆：山骰失败反弹发生在盟友领土内部，不算"盟友边界尝试"。
    lwtest::atXY(w.map, 2, 1).mountain = true;
    w.reg.get<comp::Position>(e) = comp::Position{1.9, 1.9};
    w.reg.get<comp::Velocity>(e).angle = 0.0;
    w.rng.results = {};  // chance → false：山骰失败 → 反弹
    w.rng.next = 0;
    moveOnce(w, e);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::AllyGate>(e).cusum, 0.0);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);
}

// 端到端：己方 40 格地块被盟友领土完全包围 → 兵必须在有限时间内借道通行（不再永久滞留）。
TEST(Movement, AllyGateFreesEnclosedFortyCellRegion) {
    TestWorld w(16, 14);
    setupEnclosedFortyCellPocket(w);
    constexpr int kOwnX0 = 4, kOwnY0 = 4, kOwnW = 8, kOwnH = 5;
    // 从地块内部非对称相位出发（避开格心对称轨道）。
    auto e = addArmy(w, kOwnX0 + kOwnW * 0.37, kOwnY0 + kOwnH * 0.61, 1, ArmyType::normal, 0.3,
                     0.15, true);

    bool alarmed = false;
    // 硬上界 3600 tick = 60 s @60 tick：超时即视为"滞留"。
    const int escapeTick = tickUntilLeavesOwnLand(w, e, 3600, &alarmed);
    EXPECT_TRUE(alarmed);        // 闸门确实报警（而不是从顶点漏出去）
    EXPECT_GT(escapeTick, 0);    // 有限时间内通行
    EXPECT_LE(escapeTick, 600);  // 且远快于"永久滞留"（10 s @60 tick）
    // 借道不征服：盟友领土归属不变。
    EXPECT_EQ(lwtest::atXY(w.map, kOwnX0 - 1, kOwnY0).belongi, 2);
    EXPECT_EQ(w.factions[1].landCount, kOwnW * kOwnH);
}

// 判据对兵速自适应（同一场景、不同兵速）：脱困的**路程**相同，时间 ∝ 1/速度。
// 覆盖普通兵 0.15、先锋 0.3、以及更慢的 0.075/0.09（0.6×）与更快的 0.45 —— 也即
// "未来兵种速度可变"时的行为：判据按每格尝试次数计，速度只改变用时，不改变是否/何时（按路程）放行。
TEST(Movement, AllyGateReleaseDistanceIndependentOfArmySpeed) {
    double refDist = -1.0;
    for (double speed : {0.075, 0.09, 0.15, 0.30, 0.45}) {
        TestWorld w(16, 14);
        setupEnclosedFortyCellPocket(w);
        auto e = addArmy(w, 4.0 + 8 * 0.37, 4.0 + 5 * 0.61, 1, ArmyType::normal, 0.3, speed, true);
        // 最慢 0.075 格/tick 时路程 ~30 格 → 约 420 tick，给 10 倍余量。
        const int escapeTick = tickUntilLeavesOwnLand(w, e, 40000);
        ASSERT_GT(escapeTick, 0) << "speed=" << speed;
        const double distance = escapeTick * speed;  // 近似行进路程（判据的"每格"口径）
        if (refDist < 0.0) refDist = distance;
        EXPECT_NEAR(distance, refDist, 1.0) << "speed=" << speed;  // 各路速度下路程一致
    }
}

// 非盟友边界（敌方领土的反弹）不喂养闸门。
TEST(Movement, AllyGateIgnoresEnemyBounce) {
    TestWorld w(6, 6);
    lwtest::atXY(w.map, 1, 1).land = true;
    lwtest::atXY(w.map, 1, 1).belongi = 1;
    lwtest::atXY(w.map, 2, 1).land = true;
    lwtest::atXY(w.map, 2, 1).belongi = 3;  // 敌方（无联盟）
    w.factions[1].landCount = 1;
    w.factions[3].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true);
    w.rng.results = {true};  // 敌方反弹骰成功 → 反弹
    moveOnce(w, e);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::AllyGate>(e).cusum, 0.0);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);
}

// ---- 地图边界反弹（Phase 9 补：边角反弹）----
// 兵冲向地图四边 → 对应墙壁反射；不越界、位置留在界内（jitter ~±0.0034 rad，容差 0.02）。

TEST(Movement, BouncesOffLeftWall) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            lwtest::atXY(w.map, x, y).land = true;
            lwtest::atXY(w.map, x, y).belongi = 1;
        }
    // x=0.2 向左冲左墙 → 反弹向右，回弹走了 0.1。
    auto e = addArmy(w, 0.2, 5.5, 1, ArmyType::normal, kPi, 0.3, true);
    moveOnce(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 0.02);  // π → 0
    EXPECT_GT(w.reg.get<comp::Position>(e).x, 0.0);
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 0.3);
}

TEST(Movement, BouncesOffTopWall) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            lwtest::atXY(w.map, x, y).land = true;
            lwtest::atXY(w.map, x, y).belongi = 1;
        }
    // y=0.2 向上冲顶墙 → 反弹向下。
    auto e = addArmy(w, 5.5, 0.2, 1, ArmyType::normal, -kPi / 2, 0.3, true);
    moveOnce(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi / 2, 0.02);  // -π/2 → π/2
    EXPECT_GT(w.reg.get<comp::Position>(e).y, 0.0);
    EXPECT_LT(w.reg.get<comp::Position>(e).y, 0.3);
}

TEST(Movement, BouncesOffRightAndBottomWall) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            lwtest::atXY(w.map, x, y).land = true;
            lwtest::atXY(w.map, x, y).belongi = 1;
        }
    auto e = addArmy(w, 9.8, 5.5, 1, ArmyType::normal, 0.0, 0.3, true);
    moveOnce(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 0 → π
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 10.0);

    auto e2 = addArmy(w, 5.5, 9.8, 1, ArmyType::normal, kPi / 2, 0.3, true);
    moveOnce(w, e2);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e2).angle, -kPi / 2, 0.02);  // π/2 → -π/2
    EXPECT_LT(w.reg.get<comp::Position>(e2).y, 10.0);
}

// ---- P10 地图边界贯通（环绕）已废除（2026-08 用户定夺）；边界统一为反弹，见 BouncesOff*。----

// ---- Combat ----

TEST(Combat, TwoEnemiesCollideBothDie) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            lwtest::atXY(w.map, x, y).land = true;
    auto a = addArmy(w, 5.0, 5.0, 1, ArmyType::normal, 0.0, 0.3, true);
    auto b = addArmy(w, 5.0, 5.0, 2, ArmyType::normal, 0.0, 0.3, true);
    w.hash.build(w.reg, w.map.geom());
    moveOnce(w, a);
    EXPECT_TRUE(w.reg.all_of<comp::Dead>(a));
    EXPECT_TRUE(w.reg.all_of<comp::Dead>(b));  // 双方同归于尽
    ASSERT_EQ(w.deaths.size(), 2u);
    EXPECT_NEAR(w.deaths[0].x, 5.0, 0.5);
    EXPECT_NEAR(w.deaths[1].y, 5.0, 0.5);
}

TEST(Combat, SameFactionDoesNotCollide) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            lwtest::atXY(w.map, x, y).land = true;
    auto a = addArmy(w, 5.0, 5.0, 1, ArmyType::normal, 0.0, 0.3, true);
    auto b = addArmy(w, 5.0, 5.0, 1, ArmyType::normal, 0.0, 0.3, true);  // 同势力
    w.hash.build(w.reg, w.map.geom());
    moveOnce(w, a);
    EXPECT_FALSE(w.reg.all_of<comp::Dead>(a));
    EXPECT_FALSE(w.reg.all_of<comp::Dead>(b));
    EXPECT_TRUE(w.deaths.empty());
}

// ---- 死亡效果（经完整 tick 验证）----

TEST(Death, BombDeathSpawnsBombEffect) {
    Simulation sim(lwtest::loadCfg(), 7);
    ASSERT_TRUE(sim.init());
    auto a = SpawnSystem::spawnArmy(sim, 30.5, 30.5, 1, ArmyType::bomb);
    auto b = SpawnSystem::spawnArmy(sim, 30.5, 30.5, 2, ArmyType::normal);
    sim.tick();
    EXPECT_FALSE(sim.registry().valid(a));  // 已销毁
    EXPECT_FALSE(sim.registry().valid(b));
    int bombs = 0;
    for (auto e : sim.registry().view<comp::CombatEffectTypeId>()) {
        if (sim.registry().get<comp::CombatEffectTypeId>(e).type == CombatEffectType::bomb) {
            ++bombs;
            const auto& pos = sim.registry().get<comp::Position>(e);
            const auto& cr = sim.registry().get<comp::Creator>(e);
            EXPECT_NEAR(pos.x, 30.5, 0.25);
            EXPECT_NEAR(pos.y, 30.5, 0.25);
            EXPECT_EQ(cr.factionId, 1);  // 创建者快照 = 死者势力
            EXPECT_NEAR(sim.registry().get<comp::CombatEffectParams>(e).p0, 2.4, 1e-12);  // 默认半径
        }
    }
    EXPECT_EQ(bombs, 1);  // 仅 a 是 bomb → 1 个爆炸特效
}

TEST(Death, GreenLaserDeathSpawnsThreeBeams) {
    Simulation sim(lwtest::loadCfg(), 7);
    ASSERT_TRUE(sim.init());
    // 两兵相邻：绿5（laser）击杀普通兵 → 应生成 3 束激光特效（主束 + ±π/11）。
    SpawnSystem::spawnArmy(sim, 30.5, 30.5, 5, ArmyType::laser);
    SpawnSystem::spawnArmy(sim, 30.5, 30.5, 1, ArmyType::normal);
    sim.tick();
    int lasers = 0;
    for (auto e : sim.registry().view<comp::CombatEffectTypeId>())
        if (sim.registry().get<comp::CombatEffectTypeId>(e).type == CombatEffectType::laser) ++lasers;
    EXPECT_EQ(lasers, 3);  // 绿5：主束 + ±π/11 两条
}

// ---- 无头 600 tick 冒烟 + 确定性 ----

TEST(Simulation, HeadlessArmyPhase600TicksDeterministic) {
    struct Summary {
        int land0 = 0, alive0 = 0, land1 = 0, alive1 = 0;
        bool pairDied = false;
    };
    auto run = [&](std::uint32_t seed) {
        Simulation sim(lwtest::loadCfg(), seed);
        EXPECT_TRUE(sim.init());
        // 两个敌对集群（各自首都附近各 5 队）+ 一对同位置必碰兵。
        const int cx = sim.map().capitalX(0), cy = sim.map().capitalY(0);
        const int dx = sim.map().capitalX(4), dy = sim.map().capitalY(4);
        for (int k = 0; k < 5; ++k) {
            SpawnSystem::spawnArmy(sim, cx + 0.5, cy + 0.5, 1, ArmyType::normal);
            SpawnSystem::spawnArmy(sim, dx + 0.5, dy + 0.5, 2, ArmyType::normal);
        }
        auto pairA = SpawnSystem::spawnArmy(sim, 30.5, 30.5, 1, ArmyType::normal);
        auto pairB = SpawnSystem::spawnArmy(sim, 30.5, 30.5, 2, ArmyType::normal);

        auto alive = [&]() {
            int n = 0;
            for (auto e : sim.registry().view<comp::Position, comp::Collider>())
                if (!sim.registry().all_of<comp::Dead>(e)) ++n;
            return n;
        };
        auto land = [&]() {
            int n = 0;
            for (int id = 1; id <= 8; ++id)
                n += sim.factions()[static_cast<size_t>(id)].landCount;
            return n;
        };
        Summary s;
        s.land0 = land();
        s.alive0 = alive();
        for (int t = 0; t < 600; ++t) sim.tick();
        s.land1 = land();
        s.alive1 = alive();
        s.pairDied = !sim.registry().valid(pairA) && !sim.registry().valid(pairB);
        return s;
    };

    const auto r1 = run(42);
    const auto r2 = run(42);
    EXPECT_EQ(r1.land1, r2.land1);    // 确定性：领土终态一致
    EXPECT_EQ(r1.alive1, r2.alive1);  // 确定性：兵数终态一致
    EXPECT_EQ(r1.pairDied, r2.pairDied);
    EXPECT_GT(r1.land1, r1.land0);    // 领土在涨
    EXPECT_GT(r1.alive1, r1.alive0);  // 产兵在发生（Phase 5 起经济产兵，兵数净增）
    EXPECT_TRUE(r1.pairDied);         // 战斗在发生（同位置必碰对已死亡）
}

}  // namespace
