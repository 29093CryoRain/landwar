// test_events.cpp — 消息系统单测（开发计划 P4）。
// 事件通道（takeEvents 排空）、灭亡检测（含特效/兵残留不误报）、统一检测（恰一次）、
// 事件不进快照；结构化事件 → 消息文本段组装（MessageFormat）；MessageLog（上限/留存，纯逻辑）。
#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/components.h"
#include "sim/systems/SpawnSystem.h"
#include "ui/panels/MessageFormat.h"
#include "ui/panels/MessageLog.h"
#include "TestUtil.h"

namespace {

using namespace lw;

Config loadCfg() { return lwtest::loadCfg(); }

// 只保留 keep 里的势力存活（其余手动置死并清城），用于统一检测用例。
void killAllBut(Simulation& sim, std::initializer_list<int> keep) {
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
        if (std::find(keep.begin(), keep.end(), id) != keep.end()) continue;
        sim.faction(id).alive = false;
        sim.faction(id).cityIds.clear();
        sim.faction(id).cityCount = 0;
    }
}

// 把消息段拍平成"可见文本"（势力名段按 sim 里的名字展开），便于断言文案；
// "哪些段是势力名"另用 factionSpanIds 断言（这才是渲染是否正确的结构依据）。
std::string flattenSpans(const Simulation& sim, const std::vector<ui::MessageSpan>& spans) {
    std::string out;
    for (const auto& s : spans) {
        if (s.factionId >= 0) out += sim.faction(s.factionId).name;
        else out += s.literal;
    }
    return out;
}

std::vector<int> factionSpanIds(const std::vector<ui::MessageSpan>& spans) {
    std::vector<int> ids;
    for (const auto& s : spans) {
        if (s.factionId >= 0) ids.push_back(s.factionId);
    }
    return ids;
}

// ---- 事件通道 ----

TEST(Events, TakeEventsDrains) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.pushEvent({GameEventKind::Custom, 1, 0, 0, {}, "a", 1});
    sim.pushEvent({GameEventKind::Custom, 2, 0, 0, {}, "b", 1});

    auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, GameEventKind::Custom);
    EXPECT_EQ(events[0].factionId, 1);
    EXPECT_EQ(events[0].literal, "a");
    EXPECT_EQ(events[1].literal, "b");
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
    EXPECT_FALSE(sim.faction(2).alive);  // 置 alive=false
    // 文案由 UI 层按结构化字段组装：时间前缀 + "势力 " + [势力名 id] + " 被灭亡"。
    const auto spans = ui::formatGameEvent(sim, events[0]);
    EXPECT_EQ(flattenSpans(sim, spans), "(0s) 势力 黄 被灭亡");
    EXPECT_EQ(factionSpanIds(spans), (std::vector<int>{2}));
}

TEST(Annihilation, ResidualEffectPreventsAnnihilation) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.faction(2).cityIds.clear();
    sim.faction(2).cityCount = 0;
    // 地雷特效残留 → 不算灭亡（思路 6.0.5：效果也全部消失才灭亡）。
    SpawnSystem::spawnCombatEffect(sim, 10.0, 10.0, 2, CombatEffectType::mine, 0.0,
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
    killAllBut(sim, {3});
    sim.detectAnnihilationAndUnification();
    const auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GameEventKind::Unification);
    EXPECT_EQ(events[0].factionId, 3);
    EXPECT_EQ(events[0].data, -1);  // 单势力统一：无联盟号
    EXPECT_EQ(events[0].factionIds, (std::vector<int>{3}));
    // 结构化文案：单势力统一不出现"联盟"，势力名以 id 段表达。
    const auto spans = ui::formatGameEvent(sim, events[0]);
    EXPECT_EQ(flattenSpans(sim, spans), "(0s) 势力 青 统一天下");
    EXPECT_EQ(factionSpanIds(spans), (std::vector<int>{3}));

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

// 剩余多个存活势力**同属一个联盟** → 该联盟共同统一（文案列成员；相关势力 = 首个存活成员）。
TEST(Unification, SameAllianceSurvivorsEmitAllianceWin) {
    lw::Options opts;
    opts.alliances = {{2, 4}};
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init(opts));
    ASSERT_EQ(sim.faction(2).allianceId, 0);
    ASSERT_EQ(sim.faction(4).allianceId, 0);
    killAllBut(sim, {2, 4});

    sim.detectAnnihilationAndUnification();
    const auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GameEventKind::Unification);
    EXPECT_EQ(events[0].factionId, 2);  // 首个存活成员（事件主体）
    EXPECT_EQ(events[0].data, 0);       // 联盟号 allianceId
    // 全部存活成员以 id 列表携带（顺序 = selectedFactionIds_）→ 每个名字各自着色。
    EXPECT_EQ(events[0].factionIds, (std::vector<int>{2, 4}));
    const auto spans = ui::formatGameEvent(sim, events[0]);
    EXPECT_EQ(flattenSpans(sim, spans), "(0s) 联盟 1（黄、蓝）统一天下");
    EXPECT_EQ(factionSpanIds(spans), (std::vector<int>{2, 4}));

    // 恰一次。
    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());
}

// 剩余多个存活势力**分属不同联盟** → 未统一。
TEST(Unification, DifferentAlliancesNotUnified) {
    lw::Options opts;
    opts.alliances = {{2}, {4}};
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init(opts));
    ASSERT_NE(sim.faction(2).allianceId, sim.faction(4).allianceId);
    killAllBut(sim, {2, 4});
    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());
}

// 剩余多个存活势力**都未入盟**（互为敌人）→ 未统一。
TEST(Unification, AllianceLessSurvivorsNotUnified) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    ASSERT_EQ(sim.faction(2).allianceId, -1);
    ASSERT_EQ(sim.faction(4).allianceId, -1);
    killAllBut(sim, {2, 4});
    sim.detectAnnihilationAndUnification();
    EXPECT_TRUE(sim.takeEvents().empty());
}

TEST(Unification, SnapshotPreservesEmittedState) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    killAllBut(sim, {3});
    sim.setTickCount(1);
    sim.detectAnnihilationAndUnification();
    ASSERT_EQ(sim.takeEvents().size(), 1u);
    ASSERT_EQ(sim.stats().cutoffTick, 1u);

    lw::Simulation loaded;
    std::string err;
    ASSERT_TRUE(Snapshot::deserialize(loaded, Snapshot::serialize(sim), &err)) << err;
    loaded.detectAnnihilationAndUnification();
    EXPECT_TRUE(loaded.takeEvents().empty());
    EXPECT_EQ(loaded.stats().cutoffTick, 1u);
}

// ---- 事件不进快照 ----

TEST(Events, NotSerializedInSnapshot) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    sim.pushEvent({GameEventKind::Custom, 1, 0, 0, {}, "not persisted", 5});
    EXPECT_EQ(sim.takeEvents().size(), 1u);

    const std::string json = Snapshot::serialize(sim);
    lw::Simulation loaded;
    ASSERT_TRUE(Snapshot::deserialize(loaded, json));
    EXPECT_TRUE(loaded.takeEvents().empty());  // 纯展示通道，不进存档
}

// ---- 结构化事件 → 消息文本段（UI 层组装；模拟核心不发文案）----

TEST(MessageFormat, AnnihilatedUsesFactionIdSpanNotTextSearch) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    GameEvent ev;
    ev.kind = GameEventKind::FactionAnnihilated;
    ev.factionId = 2;
    ev.tick = 600;  // 600/60 = 10s
    const auto spans = ui::formatGameEvent(sim, ev);
    EXPECT_EQ(flattenSpans(sim, spans), "(10s) 势力 黄 被灭亡");
    EXPECT_EQ(factionSpanIds(spans), (std::vector<int>{2}));
}

TEST(MessageFormat, TechAcquiredCarriesTechNameAndLevel) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    ASSERT_FALSE(sim.config().tech.techs.empty());
    const std::string techName = sim.config().tech.techs[0].name;
    GameEvent ev;
    ev.kind = GameEventKind::TechAcquired;
    ev.factionId = 1;
    ev.data = 0;
    ev.level = 2;
    const auto spans = ui::formatGameEvent(sim, ev);
    EXPECT_EQ(flattenSpans(sim, spans), "(0s) 势力 红 获得科技 " + techName + "(2级)");
    EXPECT_EQ(factionSpanIds(spans), (std::vector<int>{1}));
}

TEST(MessageFormat, OutOfRangeTechIndexFallsBackWithoutCrash) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    GameEvent ev;
    ev.kind = GameEventKind::TechAcquired;
    ev.factionId = 1;
    ev.data = 9999;
    ev.level = 1;
    const auto spans = ui::formatGameEvent(sim, ev);
    EXPECT_NE(flattenSpans(sim, spans).find("?("), std::string::npos);
    EXPECT_EQ(factionSpanIds(spans), (std::vector<int>{1}));
}

TEST(MessageFormat, CustomUsesLiteralVerbatim) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    GameEvent ev;
    ev.kind = GameEventKind::Custom;
    ev.literal = "自定义消息";
    ev.tick = 120;
    const auto spans = ui::formatGameEvent(sim, ev);
    EXPECT_EQ(flattenSpans(sim, spans), "自定义消息");  // 自定义事件自带完整文案
}

// ---- MessageLog（纯逻辑，P4 修正：无限留存，仅超上限丢最旧）----

TEST(MessageLog, MessagesPersistIndefinitely) {
    lw::ui::MessageLog log;
    log.add({{"m", -1}}, 0);        // tick=0（很早的消息）
    log.add({{"m2", -1}}, 10000);   // tick=10000（很晚的消息）
    log.prune(12);                  // 上限内 → 全部保留（不按时间过期）
    ASSERT_EQ(log.messages().size(), 2u);
    ASSERT_EQ(log.messages()[0].spans.size(), 1u);
    EXPECT_EQ(log.messages()[0].spans[0].literal, "m");
    EXPECT_EQ(log.messages()[1].spans[0].literal, "m2");
}

TEST(MessageLog, PruneDropsOldestWhenOverMax) {
    lw::ui::MessageLog log;
    for (int i = 0; i < 5; ++i)
        log.add({{"m" + std::to_string(i), -1}}, static_cast<unsigned>(i));
    log.prune(3);  // 上限 3 → 只留最后 3 条（最旧 2 条丢弃）
    const auto& ms = log.messages();
    ASSERT_EQ(ms.size(), 3u);
    EXPECT_EQ(ms[0].spans[0].literal, "m2");
    EXPECT_EQ(ms[2].spans[0].literal, "m4");
}

TEST(MessageLog, Clear) {
    lw::ui::MessageLog log;
    log.add({{"m", -1}}, 0);
    log.clear();
    EXPECT_TRUE(log.empty());
}

// 势力名以 id 段保存（联盟共同统一有多个名字）→ 渲染端按 id 着色，不做字符串匹配。
TEST(MessageLog, KeepsFactionNameSpans) {
    lw::ui::MessageLog log;
    log.add({{"联盟 1（", -1}, {"", 2}, {"、", -1}, {"", 4}, {"）统一天下", -1}}, 5);
    log.add({{"势力 ", -1}, {"", 1}, {" 被灭亡", -1}}, 6);
    ASSERT_EQ(log.messages().size(), 2u);
    const auto& alliance = log.messages()[0].spans;
    ASSERT_EQ(alliance.size(), 5u);
    EXPECT_EQ(alliance[0].literal, "联盟 1（");
    EXPECT_EQ(alliance[1].factionId, 2);
    EXPECT_EQ(alliance[3].factionId, 4);
    EXPECT_EQ(alliance[4].literal, "）统一天下");
    const auto& annihilated = log.messages()[1].spans;
    ASSERT_EQ(annihilated.size(), 3u);
    EXPECT_EQ(annihilated[1].factionId, 1);
}

}  // namespace
