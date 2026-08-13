// test_events.cpp — 消息系统单测（开发计划 P4）。
// 事件通道（takeEvents 排空）、灭亡检测（含特效/兵残留不误报）、统一检测（恰一次）、
// 事件不进快照；MessageLog（过期/上限/渐隐 alpha，纯逻辑可单测）。
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/Config.h"
#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/components.h"
#include "sim/systems/SpawnSystem.h"
#include "ui/panels/MessageLog.h"
#include "TestUtil.h"

namespace {

using namespace lw;

Config loadCfg() { return lwtest::loadCfg(); }

// ---- 事件通道 ----

TEST(Events, TakeEventsDrains) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.pushEvent({GameEventKind::Custom, 1, 0, "a", 1});
    sim.pushEvent({GameEventKind::Custom, 2, 0, "b", 1});

    auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].text, "a");
    EXPECT_EQ(events[1].text, "b");
    EXPECT_TRUE(sim.takeEvents().empty());  // 取后清空
}

// ---- 灭亡检测 ----

TEST(Annihilation, ZeroCitiesNoArmyNoEffectEmitsEvent) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    // 势力2 开局 1 城、无兵无特效 → 清空城市即达灭亡条件。
    sim.faction(2).cityIds.clear();
    sim.faction(2).cityCount = 0;

    sim.detectAnnihilationAndUnification();
    const auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GameEventKind::FactionAnnihilated);
    EXPECT_EQ(events[0].factionId, 2);
    EXPECT_NE(events[0].text.find("黄"), std::string::npos);  // 文案含势力名
    EXPECT_NE(events[0].text.find("(0s)"), std::string::npos);  // 含事件时间前缀（tick=0 → 0s）
    EXPECT_FALSE(sim.faction(2).alive);                       // 置 alive=false
}

TEST(Annihilation, ResidualEffectPreventsAnnihilation) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.faction(2).cityIds.clear();
    sim.faction(2).cityCount = 0;
    // 地雷特效残留 → 不算灭亡（思路 6.0.5：效果也全部消失才灭亡）。
    SpawnSystem::spawnEffect(sim, 10.0, 10.0, 2, EffectType::mine, 0.0,
                             comp::Creator{10.0, 10.0, 2});

    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());
    EXPECT_TRUE(sim.faction(2).alive);
}

TEST(Annihilation, ResidualArmyPreventsAnnihilation) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.faction(2).cityIds.clear();
    sim.faction(2).cityCount = 0;
    SpawnSystem::spawnArmy(sim, 10.0, 10.0, 2, ArmyType::normal);

    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());
    EXPECT_TRUE(sim.faction(2).alive);
}

TEST(Annihilation, CityRemainingPreventsAnnihilation) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    // 势力2 仍持首都（cityCount=1）→ 不灭亡。
    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());
    EXPECT_TRUE(sim.faction(2).alive);
}

TEST(Annihilation, DisabledFactionNotReEmitted) {
    lw::Options opts;
    opts.factions[1].enabled = false;  // 势力2 禁用（init 即 alive=false）
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init(opts));
    EXPECT_FALSE(sim.faction(2).alive);
    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());  // 禁用势力不报灭亡
}

// ---- 统一检测 ----

TEST(Unification, OnlyOneAliveEmitsOnce) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    // 只留势力3 alive（其余手动置死并清城）。
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
        if (id == 3) continue;
        sim.faction(id).alive = false;
        sim.faction(id).cityIds.clear();
        sim.faction(id).cityCount = 0;
    }
    sim.detectAnnihilationAndUnification();
    const auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GameEventKind::Unification);
    EXPECT_EQ(events[0].factionId, 3);
    EXPECT_NE(events[0].text.find("青"), std::string::npos);       // 文案含势力名
    EXPECT_NE(events[0].text.find("(0s)"), std::string::npos);     // 含事件时间前缀

    // 再次检测不再发（恰一次）。
    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());
}

TEST(Unification, NotEmittedWhenMultipleAlive) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());  // 8 个势力都 alive
}

// ---- 事件不进快照 ----

TEST(Events, NotSerializedInSnapshot) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.pushEvent({GameEventKind::Custom, 1, 0, "not persisted", 5});
    EXPECT_EQ(sim.takeEvents().size(), 1u);

    const std::string json = Snapshot::serialize(sim);
    lw::Simulation loaded;
    ASSERT_TRUE(Snapshot::deserialize(loaded, json));
    EXPECT_TRUE(loaded.takeEvents().empty());  // 纯展示通道，不进存档
}

// ---- MessageLog（纯逻辑，P4 修正：无限留存，仅超上限丢最旧）----

TEST(MessageLog, MessagesPersistIndefinitely) {
    lw::ui::MessageLog log;
    log.add("m", 0, 1);        // tick=0（很早的消息）
    log.add("m2", 10000, 2);   // tick=10000（很晚的消息）
    log.prune(12);             // 上限内 → 全部保留（不按时间过期）
    ASSERT_EQ(log.messages().size(), 2u);
    EXPECT_EQ(log.messages()[0].text, "m");
    EXPECT_EQ(log.messages()[1].text, "m2");
}

TEST(MessageLog, PruneDropsOldestWhenOverMax) {
    lw::ui::MessageLog log;
    for (int i = 0; i < 5; ++i) log.add("m" + std::to_string(i), static_cast<unsigned>(i), 1);
    log.prune(3);  // 上限 3 → 只留最后 3 条（最旧 2 条丢弃）
    const auto& ms = log.messages();
    ASSERT_EQ(ms.size(), 3u);
    EXPECT_EQ(ms[0].text, "m2");
    EXPECT_EQ(ms[2].text, "m4");
}

TEST(MessageLog, Clear) {
    lw::ui::MessageLog log;
    log.add("m", 0, 1);
    log.clear();
    EXPECT_TRUE(log.empty());
}

}  // namespace
