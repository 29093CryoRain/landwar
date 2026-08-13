// test_options.cpp — 菜单选项（Options）、势力出场开关、产兵 AI 分派、--no-menu 单测（开发计划 P1）。
#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

#include <entt/entt.hpp>

#include "core/Options.h"
#include "core/Simulation.h"
#include "replay/Cli.h"
#include "sim/components.h"
#include "sim/systems/ProductionSystem.h"
#include "TestUtil.h"

namespace {

using namespace lw;  // lw::comp::Position 等直接用 comp::

Config loadCfg() { return lwtest::loadCfg(); }

void clearArmies(lw::Simulation& sim) {
    auto& reg = sim.registry();
    std::vector<entt::entity> all;
    for (auto e : reg.view<comp::Position, comp::Collider>()) all.push_back(e);
    for (auto e : all) reg.destroy(e);
}

// ---- Options 序列化 / 校验 ----

TEST(Options, DefaultsAllEnabledDefaultAI) {
    lw::Options o;
    for (int i = 0; i < lw::kPlayerFactionCount; ++i) {
        EXPECT_TRUE(o.factions[static_cast<size_t>(i)].enabled) << "faction slot " << i;
        EXPECT_EQ(o.factions[static_cast<size_t>(i)].aiId, 0) << "faction slot " << i;
    }
    EXPECT_EQ(o.map.file, "data/map_bigIslands.bmp");
    EXPECT_EQ(o.map.kind, lw::MapSelection::Kind::File);
    EXPECT_FALSE(o.map.wrap);
}

TEST(Options, ToJsonRoundTrip) {
    lw::Options a;
    a.factions[0].enabled = false;
    a.factions[3].aiId = 1;
    a.map.file = "data/map3.bmp";
    a.map.wrap = true;
    a.map.randomSeed = 1234u;
    a.map.kind = lw::MapSelection::Kind::Random;
    a.map.width = 88;
    a.map.height = 66;
    a.map.seaRatio = 0.55;
    a.map.mountainDensity = 0.12;
    a.map.cityDensity = 0.03;
    a.map.forceCoast = true;

    const lw::Options b = lw::Options::loadFromJson(a.toJson());
    EXPECT_FALSE(b.factions[0].enabled);
    EXPECT_EQ(b.factions[3].aiId, 1);
    EXPECT_EQ(b.map.file, "data/map3.bmp");
    EXPECT_TRUE(b.map.wrap);
    EXPECT_EQ(b.map.randomSeed, 1234u);
    EXPECT_EQ(b.map.kind, lw::MapSelection::Kind::Random);
    EXPECT_EQ(b.map.width, 88);
    EXPECT_EQ(b.map.height, 66);
    EXPECT_DOUBLE_EQ(b.map.seaRatio, 0.55);
    EXPECT_DOUBLE_EQ(b.map.mountainDensity, 0.12);
    EXPECT_DOUBLE_EQ(b.map.cityDensity, 0.03);
    EXPECT_TRUE(b.map.forceCoast);
    // 未改动的槽位保持默认。
    EXPECT_TRUE(b.factions[1].enabled);
    EXPECT_EQ(b.factions[1].aiId, 0);
}

TEST(Options, MapRandomParamsDefaultAndRoundTrip) {
    // 旧 options.json（无随机图参数键）→ 回退默认。
    lw::Options o = lw::Options::loadFromJson(R"({"map":{"kind":"file","file":"a.bmp"}})");
    EXPECT_EQ(o.map.width, 105);
    EXPECT_EQ(o.map.height, 95);
    EXPECT_DOUBLE_EQ(o.map.seaRatio, 0.40);
    EXPECT_DOUBLE_EQ(o.map.mountainDensity, 0.08);
    EXPECT_DOUBLE_EQ(o.map.cityDensity, 0.02);
    // 键严格对称 → 解析再序列化逐字节一致。
    const std::string j1 = o.toJson();
    const std::string j2 = lw::Options::loadFromJson(j1).toJson();
    EXPECT_EQ(j1, j2);
}

TEST(Options, MissingFileFallsBackToDefaults) {
    const lw::Options o = lw::Options::loadFromFile("tests/data/nonexistent_options.json");
    for (int i = 0; i < lw::kPlayerFactionCount; ++i) {
        EXPECT_TRUE(o.factions[static_cast<size_t>(i)].enabled);
        EXPECT_EQ(o.factions[static_cast<size_t>(i)].aiId, 0);
    }
    EXPECT_EQ(o.map.file, "data/map_bigIslands.bmp");
    EXPECT_EQ(o.map.kind, lw::MapSelection::Kind::File);
}

TEST(Options, SaveThenLoadRoundTrip) {
    lw::Options a;
    a.factions[2].enabled = false;
    a.factions[5].aiId = 1;
    a.map.file = "data/map2.bmp";
    const std::string path = "tests/data/options_test.json";
    a.saveToFile(path);
    const lw::Options b = lw::Options::loadFromFile(path);
    EXPECT_FALSE(b.factions[2].enabled);
    EXPECT_EQ(b.factions[5].aiId, 1);
    EXPECT_EQ(b.map.file, "data/map2.bmp");
    std::remove(path.c_str());
}

TEST(Options, ValidatePlayerLimit) {
    lw::Options a;
    EXPECT_TRUE(a.validate());            // 无玩家 → 有效
    a.factions[0].aiId = 1;
    EXPECT_TRUE(a.validate());            // 一个玩家 → 有效
    a.factions[1].aiId = 1;
    std::string err;
    EXPECT_FALSE(a.validate(&err));       // 两个玩家 → 无效
    EXPECT_FALSE(err.empty());
}

// ---- Simulation::init(options)：势力出场开关 ----

TEST(OptionsSim, DisabledFactionHasNoCapitalAndStaysNeutral) {
    lw::Options opts;
    opts.factions[1].enabled = false;  // 势力2（下标1）禁用

    lw::Simulation a(loadCfg(), 42);
    ASSERT_TRUE(a.init(opts));
    lw::Simulation b(loadCfg(), 42);
    ASSERT_TRUE(b.init());  // 全启用对照

    // 禁用势力：alive=false、无城市、无领地。
    EXPECT_FALSE(a.faction(2).alive);
    EXPECT_EQ(a.faction(2).cityCount, 0);
    EXPECT_EQ(a.faction(2).landCount, 0);
    EXPECT_TRUE(b.faction(2).alive);
    EXPECT_EQ(b.faction(2).cityCount, 1);

    // 其首都保持中立：中立势力 landCount 比全启用多 1；总城市数不变（首都仍在图里）。
    EXPECT_EQ(a.faction(0).landCount, b.faction(0).landCount + 1);
    EXPECT_EQ(a.map().totalCities(), b.map().totalCities());
}

TEST(OptionsSim, SameOptionsSameSeedDeterministic) {
    lw::Options opts;
    opts.factions[4].enabled = false;
    lw::Simulation a(loadCfg(), 7), b(loadCfg(), 7);
    ASSERT_TRUE(a.init(opts));
    ASSERT_TRUE(b.init(opts));
    EXPECT_EQ(a.map().capitalCount(), b.map().capitalCount());
    for (int i = 0; i < a.map().capitalCount(); ++i) {
        EXPECT_EQ(a.map().capitalX(i), b.map().capitalX(i)) << "capital " << i;
        EXPECT_EQ(a.map().capitalY(i), b.map().capitalY(i)) << "capital " << i;
    }
    EXPECT_EQ(a.map().totalCities(), b.map().totalCities());
}

TEST(OptionsSim, DisablingFactionDoesNotChangeRngStream) {
    // 禁用势力只跳过 init 阶段的"征服"（不消耗 RNG），地图/首都放置的 RNG 序列与全启用一致。
    lw::Options opts;
    opts.factions[2].enabled = false;
    lw::Simulation a(loadCfg(), 99), full(loadCfg(), 99);
    ASSERT_TRUE(a.init(opts));
    ASSERT_TRUE(full.init());
    for (int i = 0; i < full.map().capitalCount(); ++i) {
        EXPECT_EQ(a.map().capitalX(i), full.map().capitalX(i)) << "capital " << i;
        EXPECT_EQ(a.map().capitalY(i), full.map().capitalY(i)) << "capital " << i;
    }
}

// ---- 产兵 AI 分派 ----

TEST(ProductionDispatch, PlayerAiStubProducesNothingDefaultAiProduces) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    auto& f = sim.faction(1);
    const int c1 = sim.map().addCity(1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    f.economy = 6.0;
    f.producedCost.fill(0.0);
    f.armyCost.fill(1.0);

    f.aiId = 1;  // 玩家占位（P2 实现前不产兵）
    lw::ProductionSystem::produce(sim, 1);
    EXPECT_EQ(f.numArmyProduced, 0);
    EXPECT_DOUBLE_EQ(f.economy, 6.0);  // 未消耗

    f.aiId = 0;  // 默认 AI（公平调度堆）
    lw::ProductionSystem::produce(sim, 1);
    EXPECT_EQ(f.numArmyProduced, 6);
    EXPECT_DOUBLE_EQ(f.economy, 0.0);
}

// ---- CLI --no-menu ----

TEST(Cli, NoMenuFlagParses) {
    {
        char* argv[] = {const_cast<char*>("landwar"), const_cast<char*>("--no-menu")};
        const lw::CliOptions o = lw::parseCli(2, argv);
        EXPECT_TRUE(o.noMenu);
        EXPECT_FALSE(o.headless);  // --no-menu 不强制无头
    }
    {
        char* argv[] = {const_cast<char*>("landwar")};
        const lw::CliOptions o = lw::parseCli(1, argv);
        EXPECT_FALSE(o.noMenu);
    }
}

}  // namespace
