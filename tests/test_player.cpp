// test_player.cpp — 玩家产兵（P2）：PlayerAI 读 PlayerIntent、意图不入快照、aiId 快照往返、
// 数字键选兵种、确定性（基线 hash 由 CLI 验证）。
#include <gtest/gtest.h>

#include <vector>

#include <entt/entt.hpp>

#include "app/InputManager.h"
#include "core/GameDefs.h"
#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/components.h"
#include "sim/systems/ProductionSystem.h"
#include "TestUtil.h"

namespace {

using namespace lw;  // comp::Position 等直接用

Config baseCfg() { return lwtest::loadCfg(); }

void clearArmies(Simulation& sim) {
    auto& reg = sim.registry();
    std::vector<entt::entity> all;
    for (auto e : reg.view<comp::Position, comp::Collider>()) all.push_back(e);
    for (auto e : all) reg.destroy(e);
}

int countFactionArmiesAt(Simulation& sim, int fid, double x, double y) {
    int n = 0;
    for (auto e : sim.registry().view<comp::Position, comp::Collider, comp::FactionId>()) {
        if (sim.registry().get<comp::FactionId>(e).value != fid) continue;
        const auto& p = sim.registry().get<comp::Position>(e);
        if (std::abs(p.x - x) < 1e-9 && std::abs(p.y - y) < 1e-9) ++n;
    }
    return n;
}

// 造一个玩家势力测试场：aiId=1、两座城（注册表）、给定库存。
void setupPlayer(Simulation& sim, double economy) {
    auto& f = sim.faction(1);
    f.aiId = 1;
    // P13：城市注册到 Map 注册表，PlayerAI 按中心坐标匹配（2026-08-07 起意图坐标 = 中心）。
    // 1 级城形状 {1,1} → 中心 (baseX+0.5, baseY+0.5)：城1 (10.5,10.5)、城2 (20.5,20.5)。
    const int c1 = sim.map().addCity(1, 10, 10);
    const int c2 = sim.map().addCity(1, 20, 20);
    f.cityIds = {c1, c2};
    f.cityCount = 2;
    f.economy = economy;
    f.armyCost.fill(1.0);
    f.producedCost.fill(0.0);
}

// ---- PlayerAI 读 PlayerIntent ----

TEST(Player, NoIntentProducesNothing) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    setupPlayer(sim, 100.0);
    sim.setPlayerIntent(PlayerIntent{});  // active=false
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(sim.faction(1).numArmyProduced, 0);
    EXPECT_DOUBLE_EQ(sim.faction(1).economy, 100.0);
}

TEST(Player, MissingTypeOrCityProducesNothing) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    setupPlayer(sim, 100.0);
    PlayerIntent intent;
    intent.active = true;
    intent.factionId = 1;
    // 未选兵种 / 未选城市
    intent.selectedType = static_cast<int>(ArmyType::laser);
    intent.cityX = intent.cityY = -1.0;
    sim.setPlayerIntent(intent);
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(sim.faction(1).numArmyProduced, 0);

    intent.selectedType = -1;
    intent.cityX = 20.5;
    intent.cityY = 20.5;
    sim.setPlayerIntent(intent);
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(sim.faction(1).numArmyProduced, 0);
}

TEST(Player, SelectsTypeAndCityOnly) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    setupPlayer(sim, 5.0);
    PlayerIntent intent;
    intent.active = true;
    intent.factionId = 1;
    intent.selectedType = static_cast<int>(ArmyType::laser);  // 只产激光
    intent.cityX = 20.5;
    intent.cityY = 20.5;  // 只在城2（中心坐标）
    sim.setPlayerIntent(intent);
    ProductionSystem::produce(sim, 1);
    auto& f = sim.faction(1);
    EXPECT_EQ(f.numArmyProduced, 5);  // 库存 5、单价 1 → 5 个
    EXPECT_EQ(countFactionArmiesAt(sim, 1, 20.5, 20.5), 5);
    EXPECT_EQ(countFactionArmiesAt(sim, 1, 10.5, 10.5), 0);  // 不产其他兵/其他城
    EXPECT_DOUBLE_EQ(f.producedCost[static_cast<size_t>(ArmyType::laser)], 5.0);
    EXPECT_DOUBLE_EQ(f.producedCost[static_cast<size_t>(ArmyType::normal)], 0.0);
    EXPECT_DOUBLE_EQ(f.economy, 0.0);
}

TEST(Player, StopsWhenEconomyInsufficient) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    setupPlayer(sim, 2.0);
    auto& f = sim.faction(1);
    f.armyCost[static_cast<size_t>(ArmyType::laser)] = 3.0;  // 激光单价 3 > 库存 2
    PlayerIntent intent;
    intent.active = true;
    intent.factionId = 1;
    intent.selectedType = static_cast<int>(ArmyType::laser);
    intent.cityX = 20.5;
    intent.cityY = 20.5;
    sim.setPlayerIntent(intent);
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(f.numArmyProduced, 0);
    EXPECT_DOUBLE_EQ(f.economy, 2.0);
}

TEST(Player, CityNotOwnedStops) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    setupPlayer(sim, 100.0);
    PlayerIntent intent;
    intent.active = true;
    intent.factionId = 1;
    intent.selectedType = static_cast<int>(ArmyType::normal);
    intent.cityX = 99.5;
    intent.cityY = 99.5;  // 不在 cities 列表（已易主/消失）
    sim.setPlayerIntent(intent);
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(sim.faction(1).numArmyProduced, 0);
}

TEST(Player, WrongFactionIntentIgnored) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    setupPlayer(sim, 100.0);
    PlayerIntent intent;
    intent.active = true;
    intent.factionId = 2;  // 意图属于别的势力 → 不产
    intent.selectedType = static_cast<int>(ArmyType::normal);
    intent.cityX = 20.5;
    intent.cityY = 20.5;
    sim.setPlayerIntent(intent);
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(sim.faction(1).numArmyProduced, 0);
}

// ---- 图标下方造价（P11 改版：势力特色不再影响价格 → 各势力同价 = 定义 baseCost）----

TEST(Player, CostUniformAcrossFactions) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    // 所有势力同价（无 costDiv 折扣）：红/黄/青/绿 的普通兵造价都 = baseCost。
    for (int fid = 1; fid <= kPlayerFactionCount; ++fid) {
        EXPECT_NEAR(sim.faction(fid).armyCost[static_cast<size_t>(ArmyType::normal)],
                    cfg.units[static_cast<size_t>(ArmyType::normal)].cost, 1e-9)
            << "faction " << fid;
        EXPECT_NEAR(sim.faction(fid).armyCost[static_cast<size_t>(ArmyType::laser)],
                    cfg.units[static_cast<size_t>(ArmyType::laser)].cost, 1e-9)
            << "faction " << fid;
    }
    // 兵种偏好系数随势力不同（红 normal=2、黄 laser=1.5、青 vanguard=3…）。
    EXPECT_EQ(sim.faction(1).unitPreference[static_cast<size_t>(ArmyType::normal)], 2.0);
    EXPECT_EQ(sim.faction(2).unitPreference[static_cast<size_t>(ArmyType::laser)], 1.5);
    EXPECT_EQ(sim.faction(3).unitPreference[static_cast<size_t>(ArmyType::vanguard)], 3.0);
}

// ---- PlayerIntent 不入快照 ----

TEST(Player, PlayerIntentNotInSnapshot) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    PlayerIntent intent;
    intent.active = true;
    intent.factionId = 1;
    intent.selectedType = static_cast<int>(ArmyType::bomb);
    intent.cityX = 10.5;
    intent.cityY = 10.5;
    sim.setPlayerIntent(intent);
    const std::string json = Snapshot::serialize(sim);
    EXPECT_EQ(json.find("playerIntent"), std::string::npos) << "意图不得进快照（外部输入）";
    // 读档后意图清空 → PlayerAI 不产兵（UI 重新建立意图）。
    Simulation sim2(cfg, 0);
    ASSERT_TRUE(Snapshot::deserialize(sim2, json));
    EXPECT_FALSE(sim2.playerIntent().active);
    EXPECT_EQ(sim2.playerIntent().selectedType, -1);
}

// ---- Faction.aiId 快照往返 ----

TEST(Player, AiIdSnapshotRoundTrip) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    sim.faction(1).aiId = 1;  // 玩家势力
    const std::string json = Snapshot::serialize(sim);
    EXPECT_NE(json.find("\"aiId\": 1"), std::string::npos) << "aiId 必须序列化";
    Simulation sim2(cfg, 0);
    ASSERT_TRUE(Snapshot::deserialize(sim2, json));
    EXPECT_EQ(sim2.faction(1).aiId, 1);  // 读档后玩家身份保留（不退回默认 AI）
}

// ---- 数字键选兵种 ----

TEST(PlayerInput, MousePositionPersistsAcrossFrames) {
    // 悬停指示闪烁根因回归：beginFrame 不得把 mouseX/Y 归零（无 motion 事件的帧用最近位置）。
    app::InputManager im;
    SDL_Event ev{};
    ev.type = SDL_MOUSEMOTION;
    ev.motion.x = 300;
    ev.motion.y = 200;
    im.beginFrame();
    im.handleEvent(ev);
    EXPECT_EQ(im.actions().mouseX, 300);
    EXPECT_EQ(im.actions().mouseY, 200);
    im.beginFrame();  // 下一帧无 motion 事件
    EXPECT_EQ(im.actions().mouseX, 300) << "鼠标位置必须在帧间保留，否则悬停指示闪烁";
    EXPECT_EQ(im.actions().mouseY, 200);
}

TEST(PlayerInput, BackquoteSelectsNone) {
    // ` 键 → 不生产（2026-08-03，与图标栏最左空框等价）。
    app::InputManager im;
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_BACKQUOTE;
    ev.key.timestamp = 1;
    im.beginFrame();
    im.handleEvent(ev);
    EXPECT_TRUE(im.actions().selectNone);
    EXPECT_EQ(im.actions().selectUnitType, -1);  // 不与数字键冲突
}

TEST(PlayerInput, NumberKeysSelectUnitType) {
    app::InputManager im;
    // 键 3 → 先锋? 不：键 1-6 对应 ArmyType 0..5。键 5 → bomb。
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_5;
    ev.key.timestamp = 1;
    im.beginFrame();
    im.handleEvent(ev);
    EXPECT_EQ(im.actions().selectUnitType, static_cast<int>(ArmyType::bomb));
    EXPECT_EQ(im.actions().speedSelect, 0);  // 键 5/6 不设倍速

    ev.key.keysym.sym = SDLK_1;
    ev.key.timestamp = 2;
    im.beginFrame();
    im.handleEvent(ev);
    EXPECT_EQ(im.actions().selectUnitType, static_cast<int>(ArmyType::normal));
    EXPECT_EQ(im.actions().speedSelect, 1);  // 键 1-4 同时保留倍速意图
}

}  // namespace
