// test_capital.cpp — P15 首都系统单测（开发计划 P15，思路 9.2）。
// 覆盖：迁都状态机（沦陷→指定→改指→期满迁都 / 无指定→窗口过期→立即迁都 / 指定城被攻破
// 不重置计时 / 有正式首都不可指定）、AI 指定（最久未破城、并列取 id 小、0 RNG）、
// 首都经济（capitalBase + cityEconomyRate，沦陷窗口无）、CapitalLost 消息、快照往返。
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/systems/CapitalSystem.h"
#include "sim/systems/EconomySystem.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

using namespace lw;

Config baseCfg() { return lwtest::loadCfg(); }

// 注册一座归 fid 所有的城市（可指定 lastCapturedTick）。
int addOwnedCity(Simulation& sim, int fid, int level, int baseX, int baseY,
                 std::uint64_t lastCaptured = 0) {
    const int cid = sim.map().addCity(level, baseX, baseY);
    sim.map().city(cid).ownerId = fid;
    sim.map().city(cid).lastCapturedTick = lastCaptured;
    return cid;
}

// ---- 状态机（迁移表驱动场景）----

// 沦陷 → 窗口内 AI 指定最久未被攻破城 → 期满正式迁都。
TEST(Capital, LostThenAiDesignatesAndRelocatesAtExpiry) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10, /*lastCaptured=*/50);
    const int c2 = addOwnedCity(sim, 1, 1, 20, 20, /*lastCaptured=*/200);
    f.cityIds = {c1, c2};
    f.cityCount = 2;
    f.capitalState.capitalCityId = -1;   // 首都已沦陷
    f.capitalState.lostTick = 100;
    f.capitalState.designatedCityId = -1;
    sim.setTickCount(150);  // 窗口内（150-100=50 < delay）
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.designatedCityId, c1);  // 最久未被攻破（lastCaptured 50 < 200）
    EXPECT_EQ(f.capitalState.lostTick, 100u);        // 指定不重置计时
    EXPECT_EQ(f.capitalState.capitalCityId, -1);     // 窗口内不迁都
    // 期满迁都。
    sim.setTickCount(100 + static_cast<int>(cfg.capital.relocationDelayTicks));
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.capitalCityId, c1);
    EXPECT_EQ(f.capitalState.designatedCityId, -1);
    EXPECT_EQ(f.capitalState.lostTick, 0u);
    EXPECT_FALSE(f.capitalState.immediateRelocate);
}

// 改指（窗口内）：先指定 c2，再改指 c1 → 期满迁到最新指定 c1。
TEST(Capital, ChangeDesignationDuringWindow) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10);
    const int c2 = addOwnedCity(sim, 1, 1, 20, 20);
    f.cityIds = {c1, c2};
    f.cityCount = 2;
    f.capitalState.capitalCityId = -1;
    f.capitalState.lostTick = 100;
    f.capitalState.designatedCityId = c2;
    sim.setTickCount(150);  // 窗口内
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.designatedCityId, c2);  // 有效指定不变（AI 不重选）
    // 改指 c1（模拟 AI 重选 / 玩家改指）。
    f.capitalState.designatedCityId = c1;
    sim.setTickCount(100 + static_cast<int>(cfg.capital.relocationDelayTicks));
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.capitalCityId, c1);  // 期满迁到最新指定
}

// 玩家势力（aiId=1）不自动指定：窗口过期无指定 → immediateRelocate；之后指定即立即迁都。
TEST(Capital, PlayerDesignateAfterWindowExpiryRelocatesImmediately) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(2);
    f.aiId = 1;  // 玩家势力：不自动指定
    const int c1 = addOwnedCity(sim, 2, 1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    f.capitalState.capitalCityId = -1;
    f.capitalState.lostTick = 100;
    sim.setTickCount(100 + static_cast<int>(cfg.capital.relocationDelayTicks));  // 期满
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.designatedCityId, -1);  // 玩家不自动指定
    EXPECT_TRUE(f.capitalState.immediateRelocate);    // 无指定 → 立即迁都态
    // 玩家点击己方城市指定 → 立即迁都。
    EXPECT_TRUE(sim.setDesignatedCapital(2, c1));
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.capitalCityId, c1);
    EXPECT_FALSE(f.capitalState.immediateRelocate);
    EXPECT_EQ(f.capitalState.lostTick, 0u);
}

// 指定城被攻破 → AI 重新指定（不重置计时）。
TEST(Capital, DesignatedCapturedReDesignatesWithoutResettingTimer) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10, /*lastCaptured=*/50);
    const int c2 = addOwnedCity(sim, 1, 1, 20, 20, /*lastCaptured=*/200);
    f.cityIds = {c1, c2};
    f.cityCount = 2;
    f.capitalState.capitalCityId = -1;
    f.capitalState.lostTick = 100;
    f.capitalState.designatedCityId = c2;
    sim.map().city(c2).ownerId = 2;  // 指定城被攻占（易主给势力2）
    sim.setTickCount(150);           // 窗口内
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.designatedCityId, c1);  // 重新指定最久未破城
    EXPECT_EQ(f.capitalState.lostTick, 100u);        // 计时不重置
}

// 存在正式首都时：不可指定、状态机不动作。
TEST(Capital, HasCapitalCannotDesignateAndNoAction) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10);
    const int c2 = addOwnedCity(sim, 1, 1, 20, 20);
    f.cityIds = {c1, c2};
    f.cityCount = 2;
    f.capitalState.capitalCityId = c1;  // 有正式首都
    EXPECT_FALSE(sim.setDesignatedCapital(1, c2));  // 不可指定
    sim.setTickCount(500);
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.capitalCityId, c1);
    EXPECT_EQ(f.capitalState.designatedCityId, -1);
    EXPECT_EQ(f.capitalState.lostTick, 0u);
}

// 正式首都整城易主 → 沦陷：capitalCityId=-1、lostTick=now、发 CapitalLost 消息（仅势力名着色）。
TEST(Capital, CapitalLostDetectedAndEmitsEvent) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int c1 = addOwnedCity(sim, 1, 1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    f.capitalState.capitalCityId = c1;
    sim.setTickCount(200);
    sim.map().city(c1).ownerId = 2;  // 首都整城易主给势力2
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.capitalCityId, -1);
    EXPECT_EQ(f.capitalState.lostTick, 200u);
    EXPECT_FALSE(f.capitalState.immediateRelocate);
    const auto events = sim.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GameEventKind::CapitalLost);
    EXPECT_EQ(events[0].factionId, 1);
    EXPECT_NE(events[0].text.find("首都被攻破"), std::string::npos);
}

// 收复旧都并重新指定 → 计时不重置，期满迁回。
TEST(Capital, RecapturedOldCapitalCanBeDesignatedAgain) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int cOld = addOwnedCity(sim, 1, 1, 10, 10, /*lastCaptured=*/0);
    const int cOther = addOwnedCity(sim, 1, 1, 20, 20, /*lastCaptured=*/500);
    f.cityIds = {cOld, cOther};
    f.cityCount = 2;
    f.capitalState.capitalCityId = -1;
    f.capitalState.lostTick = 100;
    f.capitalState.designatedCityId = -1;
    sim.setTickCount(150);  // 窗口内
    CapitalSystem::update(sim);
    // 旧都（lastCaptured=0）最久未被攻破 → AI 自动指定它（收复旧都并重新指定）。
    EXPECT_EQ(f.capitalState.designatedCityId, cOld);
    // 期满迁回旧都；计时不重置（lostTick 仍从 100 算）。
    sim.setTickCount(100 + static_cast<int>(cfg.capital.relocationDelayTicks));
    CapitalSystem::update(sim);
    EXPECT_EQ(f.capitalState.capitalCityId, cOld);
    EXPECT_EQ(f.capitalState.lostTick, 0u);
}

// ---- AI 指定（纯函数）----
TEST(Capital, AiPicksLeastRecentlyCapturedCityTieBreaksSmallerId) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int cSmall = addOwnedCity(sim, 1, 1, 10, 10, /*lastCaptured=*/50);
    const int cBig = addOwnedCity(sim, 1, 1, 20, 20, /*lastCaptured=*/50);   // 并列 → 取 id 小者
    const int cNew = addOwnedCity(sim, 1, 1, 30, 30, /*lastCaptured=*/300);
    f.cityIds = {cBig, cNew, cSmall};
    f.cityCount = 3;
    EXPECT_EQ(CapitalSystem::pickDesignatedCity(sim, 1), cSmall);  // 并列 lastCaptured → id 小者
    // 无己方城市 → -1。
    f.cityIds.clear();
    f.cityCount = 0;
    EXPECT_EQ(CapitalSystem::pickDesignatedCity(sim, 1), -1);
}

// ---- 首都经济（P14 hook：capitalIncome = capitalBase 直接加值；沦陷窗口无）----
TEST(Capital, EconomyCapitalIncomeAddsFlatBonusOnly) {
    Config cfg = baseCfg();
    cfg.economy.capitalBase = 5.0;  // 加成 = 直接加一个数值（非乘数；用户定夺）
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    const int cap = addOwnedCity(sim, 1, 4, 10, 10);   // 正式首都（级4）
    const int other = addOwnedCity(sim, 1, 1, 20, 20); // 普通城（级1）
    f.cityIds = {cap, other};
    f.cityCount = 2;
    f.landCount = 3;
    const double landInc = cfg.economy.perLandIncome * 3;
    // cityIncome 含首都（首都 level^alpha 收入照常计入，不因是首都而重复计率）。
    const double cityInc = EconomySystem::cityEconomyRate(cfg, sim.map().city(cap))
                           + EconomySystem::cityEconomyRate(cfg, sim.map().city(other));
    // 有正式首都 → capitalIncome = capitalBase（flat 加成，不 + 首都单城率——避免双计）。
    f.capitalState.capitalCityId = cap;
    f.economy = 0.0;
    EconomySystem::accumulate(sim, 1);
    EXPECT_NEAR(f.economy, landInc + cityInc + cfg.economy.capitalBase, 1e-9);
    // 沦陷窗口（无正式首都）→ 无 capitalIncome 加成。
    f.capitalState.capitalCityId = -1;
    f.economy = 0.0;
    EconomySystem::accumulate(sim, 1);
    EXPECT_NEAR(f.economy, landInc + cityInc, 1e-9);
}

// 回归（用户反馈 bug）：初始首都若是**多格城**（锚点落在地图既有 2/4/6/9 级城），init 阶段
// 单格征服只改一格 belongi → 城市不整块转移（其余基建格中立）→ 势力 cityCount=0 → 立即灭亡。
// 修复：init 后必须整城归本势力（cityCount>=1 且首都 ownerId=本势力）。
TEST(Capital, EveryFactionOwnsItsCapitalAfterInit) {
    for (std::uint32_t seed = 0; seed < 64; ++seed) {
        Simulation sim(baseCfg(), seed);
        ASSERT_TRUE(sim.init()) << "seed " << seed;
        for (int fid = 1; fid <= kPlayerFactionCount; ++fid) {
            const auto& f = sim.faction(fid);
            if (!f.alive) continue;
            EXPECT_GE(f.cityCount, 1) << "seed " << seed << " faction " << fid;
            if (f.capitalState.capitalCityId >= 0) {
                EXPECT_EQ(sim.map().city(f.capitalState.capitalCityId).ownerId, fid)
                    << "seed " << seed << " faction " << fid;
            }
        }
    }
}

// ---- 快照往返（CapitalState）----
TEST(Capital, CapitalStateSnapshotRoundTrip) {
    Simulation a(baseCfg(), 42);
    ASSERT_TRUE(a.init());
    auto& f = a.faction(1);
    f.capitalState.capitalCityId = 3;
    f.capitalState.designatedCityId = 7;
    f.capitalState.lostTick = 1234;
    f.capitalState.immediateRelocate = true;
    const std::string json = Snapshot::serialize(a);
    Simulation b;
    std::string err;
    ASSERT_TRUE(Snapshot::deserialize(b, json, &err)) << err;
    EXPECT_EQ(b.faction(1).capitalState.capitalCityId, 3);
    EXPECT_EQ(b.faction(1).capitalState.designatedCityId, 7);
    EXPECT_EQ(b.faction(1).capitalState.lostTick, 1234u);
    EXPECT_TRUE(b.faction(1).capitalState.immediateRelocate);
}

}  // namespace
