// test_projectile.cpp — P9 射弹系统单测。
// 覆盖：手枪周期正向发射（0 RNG）、霰弹随机 3 发（速度范围）、子弹超时、撞敌双亡、
// 不占领途经领地、穿海陆不减速、陆→山消失、山地留存变短、Behavior 从配置填充、快照往返。
// 测试手法（同 test_effect）：真实 init 后清空兵 + 全中立陆地 + 禁用势力产兵，
// 手动放受控兵种（冻结速度、固定朝向）→ 无 AI 干扰的确定性场景。
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <entt/entt.hpp>

#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/components.h"
#include "sim/systems/SpawnSystem.h"
#include "TestUtil.h"

namespace {

using namespace lw;

Config baseCfg() { return lwtest::loadCfg(); }

// 全中立陆地 + 清空兵 + 禁用势力产兵（消除 init 免费兵与 AI 产兵干扰）。
Simulation makeCleanSim() {
    Simulation sim(baseCfg(), 42);
    sim.init();
    for (int y = 0; y < sim.map().height(); ++y)
        for (int x = 0; x < sim.map().width(); ++x) {
            auto& c = sim.map().at(x, y);
            c.land = true;
            c.cityId = -1;
            c.mountain = false;
            c.belongi = 0;
        }
    std::vector<entt::entity> all;
    for (auto e : sim.registry().view<comp::Position, comp::Collider>()) all.push_back(e);
    for (auto e : all) sim.registry().destroy(e);
    for (int id = 1; id <= 8; ++id) sim.faction(id).alive = false;
    return sim;
}

int countBullets(const Simulation& sim) {
    int n = 0;
    for ([[maybe_unused]] auto e : sim.registry().view<comp::Projectile>()) ++n;
    return n;
}

// 放一兵并冻结（速度 0，固定朝向 angle）→ 不移动但周期动作照常触发。
entt::entity placeFrozen(Simulation& sim, double x, double y, int fid, ArmyType type,
                         double angle) {
    auto e = SpawnSystem::spawnArmy(sim, x, y, fid, type);
    sim.registry().get<comp::Velocity>(e).angle = angle;
    sim.registry().get<comp::Speed>(e).value = 0.0;
    return e;
}

// ---- 手枪 ----

TEST(Projectile, PistolFiresForwardPeriodically) {
    Simulation sim = makeCleanSim();
    placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);  // 朝向 0（正东）
    // 前 119 tick 无子弹；第 120 tick 触发。
    for (int t = 0; t < 119; ++t) {
        sim.tick();
        EXPECT_EQ(countBullets(sim), 0) << "tick " << t;
    }
    sim.tick();
    ASSERT_EQ(countBullets(sim), 1);
    for (auto e : sim.registry().view<comp::Projectile>()) {
        EXPECT_DOUBLE_EQ(sim.registry().get<comp::Velocity>(e).angle, 0.0);  // 正前方
        EXPECT_DOUBLE_EQ(sim.registry().get<comp::Speed>(e).value, 0.5);     // 恒定速度（1.5 的 1/3）
    }
}

TEST(Projectile, PistolResetsCounterAndFiresEveryPeriod) {
    Simulation sim = makeCleanSim();
    placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);
    for (int t = 0; t < 121; ++t) sim.tick();  // 第 120 tick 首射
    EXPECT_EQ(countBullets(sim), 1);
    for (int t = 0; t < 120; ++t) sim.tick();  // 到 tick 241：首弹已超时（寿命 90），第 2 弹刚射
    EXPECT_EQ(countBullets(sim), 1) << "第 2 发已发射，首弹已消亡 → 恰 1 颗在飞";
}

// ---- 霰弹 ----

TEST(Projectile, ShotgunFiresThreeRandomSpeed) {
    Simulation sim = makeCleanSim();
    placeFrozen(sim, 20.5, 20.5, 2, ArmyType::shotgun, 0.0);
    for (int t = 0; t < 180; ++t) sim.tick();  // 第 180 tick 齐射
    ASSERT_EQ(countBullets(sim), 3);
    for (auto e : sim.registry().view<comp::Projectile>()) {
        const double spd = sim.registry().get<comp::Speed>(e).value;
        EXPECT_GE(spd, 1.0 / 3.0 - 0.5 / 3.0 - 1e-9);  // 原值 (1.0-0.5) 的 1/3
        EXPECT_LE(spd, 1.0 / 3.0 + 0.5 / 3.0 + 1e-9);  // 原值 (1.0+0.5) 的 1/3
    }
}

// P9（2026-08-07）：霰弹向正前方 40° 扇形散射 + 轻规避堆叠。
TEST(Projectile, ShotgunFiresForwardConeNoClump) {
    const Config cfg = baseCfg();
    Simulation sim = makeCleanSim();
    placeFrozen(sim, 20.5, 20.5, 2, ArmyType::shotgun, 0.0);  // 朝向 0（正东）
    for (int t = 0; t < 180; ++t) sim.tick();  // 第 180 tick 齐射
    ASSERT_EQ(countBullets(sim), 3);
    // 半角 = π·(2/9)/2 = π/9（20°）；抖动 = 0.3·半角（≤半角 → 最外两颗必分居两侧）。
    const double half = kPi * cfg.units[7].bulletSpreadPIFrac / 2.0;
    const double bound = half + cfg.units[7].bulletSpreadJitterFrac * half + 1e-9;
    bool hasNeg = false, hasPos = false;
    for (auto e : sim.registry().view<comp::Projectile>()) {
        double d = std::fmod(sim.registry().get<comp::Velocity>(e).angle, 2.0 * kPi);
        if (d > kPi) d -= 2.0 * kPi;
        if (d < -kPi) d += 2.0 * kPi;
        EXPECT_LE(std::abs(d), bound) << "子弹应朝前 ±(半角+抖动) 内，不偏离正前方";
        if (d < 0) hasNeg = true;
        if (d > 0) hasPos = true;
    }
    EXPECT_TRUE(hasNeg && hasPos) << "3 颗均布扇形：不可能全偏向一侧";
}

// ---- 子弹生命周期 ----

TEST(Projectile, BulletTimesOut) {
    // 直接放一颗静止子弹（速度 0，不移动不出界）→ 纯测寿命计时。
    Simulation sim = makeCleanSim();
    SpawnSystem::spawnProjectile(sim, 50.5, 50.5, 1, ArmyType::pistol, 0.0, 0.0);
    EXPECT_EQ(countBullets(sim), 1);
    for (int t = 0; t < 89; ++t) sim.tick();  // 90 寿命 → 第 89 tick 后剩 1
    EXPECT_GE(countBullets(sim), 1);
    sim.tick();  // 归零 → 消失
    EXPECT_EQ(countBullets(sim), 0);
}

TEST(Projectile, BulletKillsEnemyBothDie) {
    Simulation sim = makeCleanSim();
    auto pistol = placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);
    auto enemy = SpawnSystem::spawnArmy(sim, 23.5, 20.5, 2, ArmyType::normal);
    sim.registry().get<comp::Speed>(enemy).value = 0.0;  // 敌兵冻结
    for (int t = 0; t < 120; ++t) sim.tick();            // 发射
    EXPECT_EQ(countBullets(sim), 1);
    bool enemyDead = false;
    for (int t = 0; t < 5 && !enemyDead; ++t) {
        sim.tick();
        enemyDead = !sim.registry().valid(enemy);
    }
    EXPECT_TRUE(enemyDead) << "子弹应在数 tick 内击杀敌兵";
    EXPECT_EQ(countBullets(sim), 0) << "子弹与敌兵同亡";
    EXPECT_TRUE(sim.registry().valid(pistol)) << "射手（手枪兵）与子弹是不同实体，不应死亡";
}

TEST(Projectile, BulletDoesNotConquerTerritory) {
    Simulation sim = makeCleanSim();  // 全中立陆地
    placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);
    for (int t = 0; t < 140; ++t) sim.tick();  // 发射后子弹东移 ≥1 格（0.5 格/tick）
    EXPECT_EQ(sim.map().at(21, 20).belongi, 0) << "子弹不占领途经领地";
    EXPECT_GT(countBullets(sim), 0) << "子弹仍在飞";
}

TEST(Projectile, BulletCrossesSeaWithoutSlow) {
    Simulation sim = makeCleanSim();
    // 正东路径铺海格（belongi 保持 0；海格征服为 no-op）。
    for (int x = 21; x <= 30; ++x) sim.map().at(x, 20).land = false;
    placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);
    for (int t = 0; t < 200; ++t) sim.tick();  // 0.5 格/tick：跨到 >30 约需 20 tick，200 tick 余量充足
    EXPECT_EQ(countBullets(sim), 1) << "子弹穿海不沉、不减速（仍存活）";
    double bx = 0;
    for (auto e : sim.registry().view<comp::Projectile, comp::Position>())
        bx = sim.registry().get<comp::Position>(e).x;
    EXPECT_GT(bx, 30.0) << "越过海格仍在向东飞";
}

TEST(Projectile, BulletDisappearsEnteringMountain) {
    Simulation sim = makeCleanSim();
    sim.map().at(21, 20).mountain = true;  // 子弹正东第一格为山
    placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);
    for (int t = 0; t < 121; ++t) sim.tick();  // 发射后立即撞山
    EXPECT_EQ(countBullets(sim), 0) << "陆→山子弹消失";
}

TEST(Projectile, BulletInMountainLifespanShorter) {
    // 全山地图：子弹始终在山地 → 每 tick 额外扣 2 寿命 → 90/3 ≈ 30 tick 内消失。
    Simulation sim = makeCleanSim();
    for (int y = 0; y < sim.map().height(); ++y)
        for (int x = 0; x < sim.map().width(); ++x) sim.map().at(x, y).mountain = true;
    placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);
    for (int t = 0; t < 120; ++t) sim.tick();  // 发射
    EXPECT_EQ(countBullets(sim), 1);
    for (int t = 0; t < 5; ++t) sim.tick();
    EXPECT_GE(countBullets(sim), 1) << "发射后短期内仍存活";
    for (int t = 0; t < 26; ++t) sim.tick();  // 共 31 tick 后（>30）
    EXPECT_EQ(countBullets(sim), 0) << "山地留存惩罚生效（寿命显著缩短）";
}

// ---- 行为抽象回归 ----

TEST(Projectile, BehaviorPopulatedFromConfig) {
    Simulation sim = makeCleanSim();
    auto laser = SpawnSystem::spawnArmy(sim, 5.5, 5.5, 1, ArmyType::laser);
    auto normal = SpawnSystem::spawnArmy(sim, 6.5, 5.5, 1, ArmyType::normal);
    auto pistol = SpawnSystem::spawnArmy(sim, 7.5, 5.5, 1, ArmyType::pistol);
    EXPECT_EQ(sim.registry().get<comp::Behavior>(laser).deathEffect, DeathEffect::laser);
    EXPECT_EQ(sim.registry().get<comp::Behavior>(normal).deathEffect, DeathEffect::none);
    EXPECT_EQ(sim.registry().get<comp::Behavior>(pistol).deathEffect, DeathEffect::none);
    EXPECT_EQ(sim.registry().get<comp::Behavior>(pistol).periodic, PeriodicAction::firePistol);
    EXPECT_EQ(sim.registry().get<comp::Behavior>(pistol).periodTicks, 120);
}

// ---- 快照往返：子弹实体 + 周期计数 ----

TEST(Projectile, SnapshotRoundTripsProjectileAndCounter) {
    Simulation sim = makeCleanSim();
    placeFrozen(sim, 20.5, 20.5, 1, ArmyType::pistol, 0.0);
    for (int t = 0; t < 125; ++t) sim.tick();  // 已发射 1 弹，counter=5
    EXPECT_EQ(countBullets(sim), 1);

    const std::string json = Snapshot::serialize(sim);
    Simulation loaded;
    std::string err;
    ASSERT_TRUE(Snapshot::deserialize(loaded, json, &err)) << err;
    EXPECT_EQ(countBullets(loaded), 1);
    EXPECT_EQ(loaded.tickCount(), sim.tickCount());

    // 续跑：加载态与直跑逐 tick 一致（P9 确定性迭代 + 快照序重建）。
    for (int i = 0; i < 200; ++i) {
        sim.tick();
        loaded.tick();
        EXPECT_EQ(countBullets(loaded), countBullets(sim)) << "tick " << (125 + i);
        EXPECT_EQ(loaded.rng().state(), sim.rng().state());
    }
}

}  // namespace
