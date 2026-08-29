// test_effect.cpp — 特效系统单测（翻新计划 §2.6 / Phase 4）。
// 爆炸征服/击杀/消亡、地雷引爆/超时（含紫7 大半径）、激光击杀/遇敌领土停止/绿5 加长；
// 2000 tick 无头冒烟 + 确定性。
// 测试手法：真实 init 后清空兵实体 + 铺可控陆地，再手动放特效与目标兵（确定性）。
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/Simulation.h"
#include "sim/components.h"
#include "sim/systems/SpawnSystem.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 把地图整片铺成陆地 + 指定归属（特效测试需可控地形）。
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

// 清空全部兵实体（保留特效），得到干净测试场（消除 init 免费兵的干扰）。
void clearArmies(Simulation& sim) {
    auto& reg = sim.registry();
    std::vector<entt::entity> all;
    for (auto e : reg.view<comp::Position, comp::Collider>()) all.push_back(e);
    for (auto e : all) reg.destroy(e);
}

int countEffects(Simulation& sim, EffectType type) {
    int n = 0;
    for (auto e : sim.registry().view<comp::EffectTypeId>())
        if (sim.registry().get<comp::EffectTypeId>(e).type == type) ++n;
    return n;
}

Config baseCfg() { return lwtest::loadCfg(); }

// 放置一枚特效（角标创建者快照 = 自身位置/势力）。
void placeEffect(Simulation& sim, double x, double y, int fid, EffectType type, double p0) {
    SpawnSystem::spawnEffect(sim, x, y, fid, type, p0, comp::Creator{x, y, fid});
}

// ---- 爆炸 ----

TEST(Effect, BombConquersCircleAndExpires) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);  // 全中立陆地
    clearArmies(sim);

    placeEffect(sim, 20.5, 20.5, 1, EffectType::bomb, 2.4);
    sim.tick();  // tt=0：r = sqrt(1)*2.4 = 2.4，0%2==0 → 征服
    EXPECT_EQ(sim.map().at(20, 20).belongi, 1);  // 圆心格
    EXPECT_EQ(sim.map().at(19, 20).belongi, 1);  // 距离 1.0 < 2.4
    EXPECT_EQ(sim.map().at(22, 22).belongi, 0);  // 距离 2.83 > 2.4 未征服
    EXPECT_EQ(sim.map().at(20, 30).belongi, 0);  // 远处未征服

    sim.tick();  // tt=1（奇数不征服）
    sim.tick();  // tt=2：r = sqrt(3)*2.4 = 4.16 → (22,22) 距离 2.83 被征服
    EXPECT_EQ(sim.map().at(22, 22).belongi, 1);

    for (int t = 0; t < 8; ++t) sim.tick();  // 到 tt=10（tt>8 已消亡）
    EXPECT_EQ(countEffects(sim, EffectType::bomb), 0);
}

TEST(Effect, BombKillsEnemiesInRadius) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);
    clearArmies(sim);

    placeEffect(sim, 30.5, 30.5, 1, EffectType::bomb, 2.4);
    auto enemy = SpawnSystem::spawnArmy(sim, 30.5, 30.5, 2, ArmyType::normal);
    ASSERT_TRUE(enemy != entt::null);

    sim.tick();  // 敌兵先动 ~0.3，炸弹 tt=0 r=2.4 击杀 → tick 末销毁
    EXPECT_FALSE(sim.registry().valid(enemy));
    // （死亡事件已由 DeathSystem 在本 tick 末消费，此处不再断言 deaths()）
}

// ---- 地雷 ----

TEST(Effect, MineTriggersAndExplodes) {
    Config cfg = baseCfg();
    cfg.effect.mine.armTicks = 0;         // 立即进入可引爆状态
    cfg.effect.mine.checkEveryTicks = 1;  // 每 tick 探测
    cfg.effect.mine.timeoutTicks = 3600;  // 不超时（隔离引爆分支）
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);
    clearArmies(sim);

    placeEffect(sim, 10.5, 10.5, 1, EffectType::mine, 0.0);
    auto enemy = SpawnSystem::spawnArmy(sim, 10.5, 10.5, 2, ArmyType::normal);
    ASSERT_TRUE(enemy != entt::null);

    sim.tick();  // tt=0：探测到敌兵 → 引爆 → 生成爆炸特效 + 地雷消亡
    EXPECT_EQ(countEffects(sim, EffectType::mine), 0);
    EXPECT_EQ(countEffects(sim, EffectType::bomb), 1);

    sim.tick();  // 爆炸 tt=1（奇数不击杀）
    sim.tick();  // 爆炸 tt=2：r = sqrt(3)*1.1 = 1.9 击杀（敌兵移动 ~0.6 仍在范围内）
    EXPECT_FALSE(sim.registry().valid(enemy));
}

TEST(Effect, MineTimeoutExplodes) {
    Config cfg = baseCfg();
    cfg.effect.mine.armTicks = 1000;  // 永不触发（隔离超时分支）
    cfg.effect.mine.timeoutTicks = 1;
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);
    clearArmies(sim);

    placeEffect(sim, 10.5, 10.5, 1, EffectType::mine, 0.0);
    sim.tick();  // tt=0：未超时 → 地雷存活
    EXPECT_EQ(countEffects(sim, EffectType::mine), 1);
    sim.tick();  // tt=1：超时 → 生成爆炸特效（统一为引爆半径 1.1）+ 地雷消亡
    EXPECT_EQ(countEffects(sim, EffectType::mine), 0);
    ASSERT_EQ(countEffects(sim, EffectType::bomb), 1);
    for (auto e : sim.registry().view<comp::EffectTypeId>()) {
        if (sim.registry().get<comp::EffectTypeId>(e).type == EffectType::bomb) {
            EXPECT_DOUBLE_EQ(sim.registry().get<comp::EffectParams>(e).p0, 1.1);
        }
    }
}

TEST(Effect, MineTriggerAtTimeoutExplodesOnlyOnce) {
    Config cfg = baseCfg();
    cfg.effect.mine.armTicks = 12;
    cfg.effect.mine.checkEveryTicks = 12;
    cfg.effect.mine.timeoutTicks = 12;
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);
    clearArmies(sim);

    placeEffect(sim, 10.5, 10.5, 1, EffectType::mine, 0.0);
    SpawnSystem::spawnArmy(sim, 10.5, 10.5, 2, ArmyType::normal);
    for (int i = 0; i < 13; ++i) sim.tick();  // elapsedTicks=12：探测和超时同 tick
    EXPECT_EQ(countEffects(sim, EffectType::bomb), 1);
}

TEST(Effect, MineTriggersOnceWithMultipleEnemies) {
    // Phase 9 修原版怪癖：多个敌兵在地雷范围内 → 每雷只产生一个爆炸特效。
    Config cfg = baseCfg();
    cfg.effect.mine.armTicks = 0;
    cfg.effect.mine.checkEveryTicks = 1;
    cfg.effect.mine.timeoutTicks = 3600;
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);
    clearArmies(sim);

    placeEffect(sim, 10.5, 10.5, 1, EffectType::mine, 0.0);
    SpawnSystem::spawnArmy(sim, 10.5, 10.5, 2, ArmyType::normal);
    SpawnSystem::spawnArmy(sim, 10.4, 10.5, 2, ArmyType::normal);
    SpawnSystem::spawnArmy(sim, 10.5, 10.6, 2, ArmyType::normal);  // 三个敌兵都在雷上

    sim.tick();  // tt=0 探测 → 首个命中即爆（break）
    EXPECT_EQ(countEffects(sim, EffectType::mine), 0);
    EXPECT_EQ(countEffects(sim, EffectType::bomb), 1);  // 只爆一次
}

TEST(Effect, PurpleMineTimeoutUsesTriggerRadius) {
    Config cfg = baseCfg();
    cfg.effect.mine.armTicks = 1000;
    cfg.effect.mine.timeoutTicks = 1;
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 0);
    clearArmies(sim);

    placeEffect(sim, 10.5, 10.5, 7, EffectType::mine, 0.0);
    sim.tick();
    sim.tick();  // 紫7 超时爆炸半径统一为引爆半径 = 1.43（2026-08；原版 1.95）
    ASSERT_EQ(countEffects(sim, EffectType::bomb), 1);
    for (auto e : sim.registry().view<comp::EffectTypeId>()) {
        if (sim.registry().get<comp::EffectTypeId>(e).type == EffectType::bomb) {
            EXPECT_DOUBLE_EQ(sim.registry().get<comp::EffectParams>(e).p0, 1.43);
        }
    }
}

// ---- 激光 ----

TEST(Effect, LaserKillsAlongBeam) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 1);  // 全己方陆地 → 光束畅行
    clearArmies(sim);

    // 光束沿 +x 从 (10.5,10.5) 发射，普通激光长 30 → 终点 (40.5,10.5)。
    placeEffect(sim, 10.5, 10.5, 1, EffectType::laser, 0.0);
    auto enemy = SpawnSystem::spawnArmy(sim, 20.5, 10.5, 2, ArmyType::normal);
    ASSERT_TRUE(enemy != entt::null);

    sim.tick();  // tt=0：光束直达 30，击杀线段上敌兵（敌兵仅偏离 ≤0.3）
    EXPECT_FALSE(sim.registry().valid(enemy));
}

TEST(Effect, LaserStopsAtEnemyTerritory) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 1);
    clearArmies(sim);
    sim.map().at(12, 10).belongi = 2;  // 敌领土：激光停在 (12,10) 边界

    placeEffect(sim, 10.5, 10.5, 1, EffectType::laser, 0.0);
    auto enemy = SpawnSystem::spawnArmy(sim, 20.5, 10.5, 2, ArmyType::normal);
    ASSERT_TRUE(enemy != entt::null);

    sim.tick();  // tt=0：光束到 (12,10) 边界停（length=1.5 < lst+2）→ 尖端格征服
    EXPECT_EQ(sim.map().at(12, 10).belongi, 1);  // 尖端敌格被征服
    EXPECT_TRUE(sim.registry().valid(enemy));     // 线段外的敌兵未死
}

TEST(Effect, NormalLaserRangeLimit) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 1);  // 全己方陆地 → 光束畅行
    clearArmies(sim);

    // 普通激光长 30：44.5 处敌兵打不到（距端点 ~4.0 > size+0.1）。
    placeEffect(sim, 10.5, 10.5, 1, EffectType::laser, 0.0);
    auto enemy = SpawnSystem::spawnArmy(sim, 44.5, 10.5, 2, ArmyType::normal);
    ASSERT_TRUE(enemy != entt::null);
    sim.tick();
    sim.tick();  // 两 tick 内敌兵最多靠近 0.6 → 仍距端点 ~3.4
    EXPECT_TRUE(sim.registry().valid(enemy));
}

TEST(Effect, GreenLaserLongerThanNormal) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 5);  // 激光只穿过自己势力（5）的陆地
    clearArmies(sim);

    // 绿5 激光长 45（×1.5）：够到 44.5 处敌兵。
    placeEffect(sim, 10.5, 10.5, 5, EffectType::laser, 0.0);
    auto enemy = SpawnSystem::spawnArmy(sim, 44.5, 10.5, 2, ArmyType::normal);
    ASSERT_TRUE(enemy != entt::null);
    sim.tick();
    EXPECT_FALSE(sim.registry().valid(enemy));
}

TEST(Effect, LaserHugsMapBoundary) {
    // Phase 9 补「激光贴界」：光束沿边界而行 / 射向边界时在边界停下，不越界、不崩溃。
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearToLand(sim, 1);  // 全己方陆地 → 光束畅行直到边界
    clearArmies(sim);

    // (a) 贴顶部边界 (y≈0.5) 沿 +x：击杀线上兵，束外兵（距线 2.0 > size+0.1）不杀。
    placeEffect(sim, 1.5, 0.5, 1, EffectType::laser, 0.0);
    auto onBeam = SpawnSystem::spawnArmy(sim, 6.5, 0.5, 2, ArmyType::normal);
    auto offBeam = SpawnSystem::spawnArmy(sim, 6.5, 2.5, 2, ArmyType::normal);
    ASSERT_TRUE(onBeam != entt::null && offBeam != entt::null);
    sim.tick();
    EXPECT_FALSE(sim.registry().valid(onBeam));
    EXPECT_TRUE(sim.registry().valid(offBeam));

    // (b) 竖直射向顶部边界：光束在 y=0 停（length≈2.5 不越界），击杀边界内线上兵。
    placeEffect(sim, 12.5, 2.5, 1, EffectType::laser, -kPi / 2);
    auto nearTop = SpawnSystem::spawnArmy(sim, 12.5, 1.0, 2, ArmyType::normal);
    ASSERT_TRUE(nearTop != entt::null);
    sim.tick();
    EXPECT_FALSE(sim.registry().valid(nearTop));

    // 生命周期内（22 tick）贴界推进不崩溃。
    for (int t = 0; t < 30; ++t) sim.tick();
    EXPECT_EQ(countEffects(sim, EffectType::laser), 0);  // 寿命结束全部消亡
}

// ---- 无头 2000 tick 冒烟 + 确定性 ----

TEST(Effect, Headless2000TicksDeterministic) {
    struct Summary {
        int land = 0;
        int alive = 0;
        int maxEffects = 0;
    };
    auto run = [&](std::uint32_t seed) {
        Simulation sim(lwtest::loadCfg(), seed);
        EXPECT_TRUE(sim.init());
        // 敌对集群逼出战斗 → 死亡 → 各类型死亡特效。
        SpawnSystem::spawnArmy(sim, 20.5, 20.5, 1, ArmyType::bomb);
        SpawnSystem::spawnArmy(sim, 21.0, 20.5, 2, ArmyType::normal);
        SpawnSystem::spawnArmy(sim, 40.5, 40.5, 3, ArmyType::laser);
        SpawnSystem::spawnArmy(sim, 41.0, 40.5, 4, ArmyType::normal);
        SpawnSystem::spawnArmy(sim, 60.5, 60.5, 5, ArmyType::mine);
        SpawnSystem::spawnArmy(sim, 61.0, 60.5, 6, ArmyType::normal);
        SpawnSystem::spawnArmy(sim, 80.5, 80.5, 1, ArmyType::normal);
        SpawnSystem::spawnArmy(sim, 80.5, 80.5, 2, ArmyType::normal);  // 同位置必碰对

        Summary s;
        int maxEffects = 0;
        for (int t = 0; t < 2000; ++t) {
            sim.tick();
            auto ev = sim.registry().view<comp::EffectTypeId>();
            maxEffects = std::max(maxEffects, static_cast<int>(std::distance(ev.begin(), ev.end())));
        }
        for (int id = 1; id <= 8; ++id) s.land += sim.factions()[static_cast<size_t>(id)].landCount;
        s.alive = 0;
        for (auto e : sim.registry().view<comp::Position, comp::Collider>())
            if (!sim.registry().all_of<comp::Dead>(e)) ++s.alive;
        s.maxEffects = maxEffects;
        return s;
    };

    const auto r1 = run(42);
    const auto r2 = run(42);
    EXPECT_EQ(r1.land, r2.land);          // 确定性：领土终态一致
    EXPECT_EQ(r1.alive, r2.alive);        // 确定性：兵数终态一致
    EXPECT_EQ(r1.maxEffects, r2.maxEffects);  // 确定性：特效峰值一致
    EXPECT_GT(r1.maxEffects, 0);          // 特效确实被触发（爆炸/激光/地雷）
}

}  // namespace
