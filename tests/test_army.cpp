// test_army.cpp — 军队系统单测（翻新计划 §2.2-§2.5 / Phase 3）。
// 移动分支用 MockRng 固定 chance 结果 + 精确位置，确定性验证下海/登陆/反弹/征服/战斗/死亡效果。
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <entt/entt.hpp>

#include "core/Simulation.h"
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
    return e;
}

void moveOnce(TestWorld& w, entt::entity e) {
    auto ctx = makeCtx(w);
    MovementSystem::moveArmy(ctx, e);
}

// ---- Spawn ----

TEST(Spawn, ArmyFieldsCorrect) {
    Simulation sim(lwtest::loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    auto e = SpawnSystem::spawnArmy(sim, 5.0, 5.0, 1, ArmyType::normal);
    ASSERT_TRUE(e != entt::null);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Position>(e).x, 5.0);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Position>(e).y, 5.0);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Speed>(e).value, 0.3);
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
    const double base = lwtest::loadCfg().army.baseSize;
    const std::vector<Case> cases = {
        {1, ArmyType::normal, 0.3, base},
        {3, ArmyType::vanguard, 0.3 * 1.5 * 2.0, base},  // 青：全速 ×1.5 + 先锋 ×2
        {3, ArmyType::laser, 0.3 * 1.5 * 0.6, base * 1.8},  // 青激光仍有 ×1.5
        {4, ArmyType::pioneer, 0.3 * 2.0, base},         // 蓝：开拓 ×2
        {5, ArmyType::laser, 0.3 * 0.6, base * 1.8},     // 绿无全速 ×1.5
        {1, ArmyType::bomb, 0.3 * 0.6, base * 1.8},
        {1, ArmyType::mine, 0.3 * 0.6, base * 1.4},
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

TEST(Spawn, AngleIsUniformRandom) {
    // Phase 9 修正：出生方向 0~2π 纯随机（去掉轴向吸附，对齐思路.txt「随机方向」）。
    // 验证：角度铺开、且落在原本会被吸附的轴向 ±0.14π 带内的兵确实存在（纯随机期望 ~56%）。
    Simulation sim(lwtest::loadCfg(), 99);
    ASSERT_TRUE(sim.init());
    const double snap = 0.14 * kPi;
    double minA = 2 * kPi, maxA = 0.0;
    int inBands = 0;  // 距任一轴向（0/π/2/π/3π/2）≤ snap 的兵数
    for (int i = 0; i < 500; ++i) {
        auto e = SpawnSystem::spawnArmy(sim, 20.0, 20.0, 1, ArmyType::normal);
        double a = sim.registry().get<comp::Velocity>(e).angle;
        while (a < 0) a += 2 * kPi;
        while (a >= 2 * kPi) a -= 2 * kPi;
        EXPECT_GE(a, 0.0);
        EXPECT_LT(a, 2 * kPi);
        minA = std::min(minA, a);
        maxA = std::max(maxA, a);
        for (int axis = 0; axis < 4; ++axis) {
            double d = std::fabs(a - axis * kPi / 2);
            if (d > kPi) d = 2 * kPi - d;  // 环形距离
            if (d <= snap) { ++inBands; break; }
        }
    }
    EXPECT_GE(maxA - minA, kPi);  // 角度铺开（非聚集）
    EXPECT_GT(inBands, 20);       // 轴向带内有兵 → 不再被吸附推开
}

// ---- Movement 分支（用精确位置 1.9 保证单 tick 恰好跨格）----

TEST(Movement, GoToSeaSucceeds) {
    TestWorld w(5, 5);
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
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
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {false};  // 下海失败 → 水平反弹
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹角 + 随机小偏置
}

TEST(Movement, LandingFromSeaRestoresSpeed) {
    TestWorld w(5, 5);
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).belongi = 0;  // 中立陆
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, false);
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);  // speed ×2 恢复
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);                    // 登陆征服
    EXPECT_EQ(w.factions[1].landCount, 1);
}

TEST(Movement, ConquersEnemyLandAndBounces) {
    TestWorld w(6, 6);
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {true};  // 反弹
    moveOnce(w, e);
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);  // 攻占成功
    EXPECT_EQ(w.factions[1].landCount, 1);
    EXPECT_EQ(w.factions[2].landCount, 0);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹角 + 随机小偏置
}

TEST(Movement, ConquersEnemyLandVanguardDoesNotBounce) {
    // 细节改进：先锋兵碰敌方领土概率不反弹（bounceMult=0.4）→ 征服后继续前进。
    TestWorld w(6, 6);
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::vanguard, 0.0, 0.3, true);
    w.rng.results = {false};  // 敌方反弹不中 → 不反弹
    moveOnce(w, e);
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 1e-9);
}

TEST(Movement, VanguardBouncesOnEnemy) {
    // 先锋也可能反弹（bounceMult=0.4 中签）：先征服再反弹（原版"弹回"语义）。
    TestWorld w(6, 6);
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::vanguard, 0.0, 0.3, true);
    w.rng.results = {true};  // 敌方阻挡命中 → 反弹
    moveOnce(w, e);
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);  // 征服已发生
    EXPECT_EQ(w.factions[1].landCount, 1);
    EXPECT_EQ(w.factions[2].landCount, 0);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹角 + 随机小偏置
}

TEST(Movement, PioneerConquersAdjacentCellsOnEnemy) {
    // 细节改进：开拓兵碰敌方领土时，上下左右 4 邻格也一起占领（正方形网格）。
    TestWorld w(6, 6);
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;   // 目标格：敌方
    w.map.at(2, 1).belongi = 2;
    w.map.at(2, 2).land = true;   // 上
    w.map.at(2, 2).belongi = 2;
    w.map.at(2, 0).land = true;   // 下
    w.map.at(2, 0).belongi = 2;
    w.map.at(3, 1).land = true;   // 右
    w.map.at(3, 1).belongi = 2;
    w.factions[1].landCount = 1;  // 原点 (1,1)
    w.factions[2].landCount = 4;  // 目标+上+下+右
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::pioneer, 0.0, 0.3, true);
    w.rng.results = {false};  // 敌方反弹不中（开拓 bounceMult=1.0 实际必反弹，此处测试连占路径）
    moveOnce(w, e);
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);  // 目标格
    EXPECT_EQ(w.map.at(2, 2).belongi, 1);  // 上
    EXPECT_EQ(w.map.at(2, 0).belongi, 1);  // 下
    EXPECT_EQ(w.map.at(3, 1).belongi, 1);  // 右
    EXPECT_EQ(w.map.at(1, 1).belongi, 1);  // 左（原点，同势力不变）
    EXPECT_EQ(w.factions[1].landCount, 5);  // 原点 1 + 新占 4
    EXPECT_EQ(w.factions[2].landCount, 0);
}

// ---- 地图边界反弹（Phase 9 补：边角反弹）----
// 兵冲向地图四边 → 对应墙壁反射；不越界、位置留在界内（jitter ~±0.0034 rad，容差 0.02）。

TEST(Movement, BouncesOffLeftWall) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
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
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
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
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
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

// ---- P10 地图边界贯通（环绕）----
// wrap 开启：撞边界 → 传送对侧（速度/方向不变，0 RNG）；关闭 = 上面四边反弹测试（回归）。

void moveOnceWrap(TestWorld& w, entt::entity e) {
    auto ctx = makeCtx(w);
    ctx.wrap = true;
    MovementSystem::moveArmy(ctx, e);
}

TEST(Movement, WrapsAcrossLeftEdge) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
        }
    // x=0.2 向左撞左墙 → 传送到右侧（≈9.2），角度/速度不变（对比 BouncesOffLeftWall 反弹）。
    auto e = addArmy(w, 0.2, 5.5, 1, ArmyType::normal, kPi, 1.0, true);
    moveOnceWrap(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 方向不变
    EXPECT_NEAR(w.reg.get<comp::Speed>(e).value, 1.0, 1e-12);    // 速度不变
    EXPECT_GT(w.reg.get<comp::Position>(e).x, 8.0);              // 出现在右侧
    EXPECT_NEAR(w.reg.get<comp::Position>(e).y, 5.5, 0.01);
}

TEST(Movement, WrapsAcrossRightEdge) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
        }
    // x=9.8 向右撞右墙 → 传送到左侧（≈0.8），角度/速度不变。
    auto e = addArmy(w, 9.8, 5.5, 1, ArmyType::normal, 0.0, 1.0, true);
    moveOnceWrap(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 0.02);
    EXPECT_NEAR(w.reg.get<comp::Speed>(e).value, 1.0, 1e-12);
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).y, 5.5, 0.01);
}

TEST(Movement, WrapsAcrossBottomEdge) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
        }
    // y=0.2 向下撞底墙 → 传送到顶部（≈9.2），角度/速度不变。
    auto e = addArmy(w, 5.5, 0.2, 1, ArmyType::normal, -kPi / 2, 1.0, true);
    moveOnceWrap(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, -kPi / 2, 0.02);
    EXPECT_NEAR(w.reg.get<comp::Speed>(e).value, 1.0, 1e-12);
    EXPECT_GT(w.reg.get<comp::Position>(e).y, 8.0);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 5.5, 0.01);
}

TEST(Movement, WrapsAcrossTopEdge) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
        }
    // y=9.8 向上撞顶墙 → 传送到底部（≈0.8），角度/速度不变。
    auto e = addArmy(w, 5.5, 9.8, 1, ArmyType::normal, kPi / 2, 1.0, true);
    moveOnceWrap(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi / 2, 0.02);
    EXPECT_NEAR(w.reg.get<comp::Speed>(e).value, 1.0, 1e-12);
    EXPECT_LT(w.reg.get<comp::Position>(e).y, 2.0);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 5.5, 0.01);
}

TEST(Movement, WrapsThenContinuesMoving) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            w.map.at(x, y).land = true;
            w.map.at(x, y).belongi = 1;
        }
    // 速度大到传送后剩余移动继续走：0.2 向左 1.5 → 撞左墙传右侧，再跨界格线 9 进 cell 8，终点 ≈8.7。
    auto e = addArmy(w, 0.2, 5.5, 1, ArmyType::normal, kPi, 1.5, true);
    moveOnceWrap(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 方向不变（无反弹）
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 8.7, 0.05);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).y, 5.5, 0.01);
}

// ---- Combat ----

TEST(Combat, TwoEnemiesCollideBothDie) {
    TestWorld w(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            w.map.at(x, y).land = true;
    auto a = addArmy(w, 5.0, 5.0, 1, ArmyType::normal, 0.0, 0.3, true);
    auto b = addArmy(w, 5.0, 5.0, 2, ArmyType::normal, 0.0, 0.3, true);
    w.hash.build(w.reg, w.map.width(), w.map.height());
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
            w.map.at(x, y).land = true;
    auto a = addArmy(w, 5.0, 5.0, 1, ArmyType::normal, 0.0, 0.3, true);
    auto b = addArmy(w, 5.0, 5.0, 1, ArmyType::normal, 0.0, 0.3, true);  // 同势力
    w.hash.build(w.reg, w.map.width(), w.map.height());
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
    for (auto e : sim.registry().view<comp::EffectTypeId>()) {
        if (sim.registry().get<comp::EffectTypeId>(e).type == EffectType::bomb) {
            ++bombs;
            const auto& pos = sim.registry().get<comp::Position>(e);
            const auto& cr = sim.registry().get<comp::Creator>(e);
            EXPECT_NEAR(pos.x, 30.5, 0.25);
            EXPECT_NEAR(pos.y, 30.5, 0.25);
            EXPECT_EQ(cr.factionId, 1);  // 创建者快照 = 死者势力
            EXPECT_NEAR(sim.registry().get<comp::EffectParams>(e).p0, 2.4, 1e-12);  // 默认半径
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
    for (auto e : sim.registry().view<comp::EffectTypeId>())
        if (sim.registry().get<comp::EffectTypeId>(e).type == EffectType::laser) ++lasers;
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
