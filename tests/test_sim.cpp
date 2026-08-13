// test_sim.cpp — Simulation 初始化与确定性单测（翻新计划 §1.4 / Phase 2）。
#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "core/Config.h"
#include "core/Simulation.h"

namespace {

lw::Config loadCfg() {
    return lw::Config::loadFromFile("data/config.json");
}

TEST(Simulation, InitBuildsFactionsAndConquersCapitals) {
    lw::Simulation sim(loadCfg(), 42);
    ASSERT_TRUE(sim.init());

    const auto& factions = sim.factions();
    ASSERT_EQ(factions.size(), static_cast<size_t>(lw::kFactionTotal));
    EXPECT_FALSE(factions[0].alive);
    for (int id = 1; id <= 8; ++id) {
        const auto& f = factions[static_cast<size_t>(id)];
        EXPECT_TRUE(f.alive) << "faction " << id;
        EXPECT_EQ(f.cityCount, 1) << "faction " << id << " 恰持一个首都";
        EXPECT_EQ(f.landCount, 1) << "faction " << id;
    }

    // 每个首都归属某个可玩势力，且 8 个首都不重复。
    std::vector<std::pair<int, int>> capitals;
    for (int i = 0; i < sim.map().capitalCount(); ++i) {
        const int x = sim.map().capitalX(i);
        const int y = sim.map().capitalY(i);
        const int owner = sim.map().at(x, y).belongi;
        EXPECT_GE(owner, 1);
        EXPECT_LE(owner, 8);
        capitals.emplace_back(x, y);
    }
    std::sort(capitals.begin(), capitals.end());
    EXPECT_EQ(std::adjacent_find(capitals.begin(), capitals.end()), capitals.end());

    EXPECT_GE(sim.map().totalCities(), 8);
    EXPECT_EQ(sim.tickCount(), 0u);
}

TEST(Simulation, SameSeedDeterministic) {
    lw::Simulation a(loadCfg(), 42);
    lw::Simulation b(loadCfg(), 42);
    ASSERT_TRUE(a.init());
    ASSERT_TRUE(b.init());

    for (int i = 0; i < a.map().capitalCount(); ++i) {
        EXPECT_EQ(a.map().capitalX(i), b.map().capitalX(i)) << "capital " << i;
        EXPECT_EQ(a.map().capitalY(i), b.map().capitalY(i)) << "capital " << i;
    }
    EXPECT_EQ(a.map().totalCities(), b.map().totalCities());

    // 整个归属网格抽查（粗间隔扫描全部 9975 格成本太高，抽样即可）。
    for (int y = 0; y < a.map().height(); y += 7) {
        for (int x = 0; x < a.map().width(); x += 11) {
            EXPECT_EQ(a.map().at(x, y).belongi, b.map().at(x, y).belongi)
                << "belongi at (" << x << "," << y << ")";
            // P13：逐格 cityId 归属（格 ∈ 某城 = cityId>=0）。
            EXPECT_EQ(a.map().at(x, y).cityId >= 0, b.map().at(x, y).cityId >= 0)
                << "city at (" << x << "," << y << ")";
        }
    }
}

TEST(Simulation, DifferentSeedProducesDifferentCapitals) {
    lw::Simulation a(loadCfg(), 1);
    lw::Simulation b(loadCfg(), 2);
    ASSERT_TRUE(a.init());
    ASSERT_TRUE(b.init());

    bool anyDiff = false;
    for (int i = 0; i < a.map().capitalCount(); ++i) {
        if (a.map().capitalX(i) != b.map().capitalX(i) ||
            a.map().capitalY(i) != b.map().capitalY(i)) {
            anyDiff = true;
            break;
        }
    }
    EXPECT_TRUE(anyDiff);  // 不同种子几乎必然产生不同首都
}

TEST(Simulation, TickIncrementsWithoutInit) {
    lw::Simulation sim;
    sim.tick();
    sim.tick();
    EXPECT_EQ(sim.tickCount(), 2u);
}

// ---- P6 RNG 分离：地图种子 vs 主种子 ----

// 逐格比较海/陆/山（海/陆由 BMP 确定、山由 mapRng 掷骰，二者都不被 placeCapitals 覆盖；
// city 会被首都覆盖，单独比较）。
bool mapDiceEqual(const lw::Map& a, const lw::Map& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const auto& ca = a.at(x, y);
            const auto& cb = b.at(x, y);
            if (ca.land != cb.land || ca.mountain != cb.mountain) return false;
        }
    }
    return true;
}

// 城市格布局是否不同（城骰子由 mapRng 掷；首都也会置 cityId，故比较须同主种子才纯净）。
bool anyCityDiff(const lw::Map& a, const lw::Map& b) {
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if ((a.at(x, y).cityId >= 0) != (b.at(x, y).cityId >= 0)) return true;
    return false;
}

TEST(Simulation, MapSeedDrivesTerrainMainSeedDrivesCapitals) {
    // 同 mapSeed、不同主种子 → 海/陆/山完全一致（地图骰子同源），仅首都/后续漂移。
    lw::Simulation a(loadCfg(), 1, 7);
    lw::Simulation b(loadCfg(), 2, 7);
    ASSERT_TRUE(a.init());
    ASSERT_TRUE(b.init());
    EXPECT_EQ(a.mapSeed(), 7u);
    EXPECT_EQ(b.mapSeed(), 7u);
    EXPECT_TRUE(mapDiceEqual(a.map(), b.map()));

    bool capDiff = false;
    for (int i = 0; i < a.map().capitalCount(); ++i) {
        if (a.map().capitalX(i) != b.map().capitalX(i) ||
            a.map().capitalY(i) != b.map().capitalY(i)) {
            capDiff = true;
            break;
        }
    }
    EXPECT_TRUE(capDiff);  // 首都用主种子，两局几乎必然不同
}

TEST(Simulation, DifferentMapSeedChangesTerrain) {
    // 同主种子、不同地图种子 → 城骰子不同 → 城市布局不同（海/陆仍确定，由 BMP）。
    lw::Simulation a(loadCfg(), 7, 1);
    lw::Simulation b(loadCfg(), 7, 2);
    ASSERT_TRUE(a.init());
    ASSERT_TRUE(b.init());
    // 海/陆确定性不受地图种子影响（由 BMP 决定）。
    for (int y = 0; y < a.map().height(); ++y) {
        for (int x = 0; x < a.map().width(); ++x) {
            EXPECT_EQ(a.map().at(x, y).land, b.map().at(x, y).land) << "(" << x << "," << y << ")";
        }
    }
    EXPECT_TRUE(anyCityDiff(a.map(), b.map()));  // 城骰子不同 → 布局几乎必然不同
}

TEST(Simulation, TwoArgCtorDefaultsMapSeedToSeed) {
    lw::Simulation sim(loadCfg(), 42);
    EXPECT_EQ(sim.mapSeed(), 42u);
}

}  // namespace
