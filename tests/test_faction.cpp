// test_faction.cpp — 势力运行时/征服/城市列表单测（翻新计划 §2.8 / Phase 2）。
#include <gtest/gtest.h>

#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "world/Faction.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

// 全陆小地图 + 9 个已初始化势力 + 待产兵表。
struct SmallWorld {
    lw::Config::Map mapCfg;
    lw::Map map;
    std::vector<lw::Faction> factions;
    std::vector<lw::PendingSpawn> pending;

    explicit SmallWorld(int width = 10, int height = 8) {
        mapCfg.width = width;
        mapCfg.height = height;
        map.configure(mapCfg);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                map.at(x, y).land = true;

        const lw::Config cfg = lw::Config::loadFromJson("{}");
        factions.resize(static_cast<size_t>(lw::kFactionTotal));
        for (int id = 0; id < lw::kFactionTotal; ++id) {
            factions[static_cast<size_t>(id)].initFromDef(cfg.factions[static_cast<size_t>(id)], cfg);
        }
        // 中立势力0 landCount 初始 = 无主领地数（全图均为中立），随征服递减至 0（2026-08 用户定夺）。
        // 与 Simulation::init 一致：只统计 land 且 belongi==0 的格（海格不计入）。
        int neutralLand = 0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (map.at(x, y).land) ++neutralLand;
        factions[0].landCount = neutralLand;
    }
};

TEST(Faction, InitFromDefComputesCosts) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    lw::Faction f;
    f.initFromDef(cfg.factions[1], cfg);
    EXPECT_EQ(f.id, 1);
    EXPECT_TRUE(f.alive);
    EXPECT_NEAR(f.economy, 1.1, 1e-12);
    // P11 改版：势力特色不再影响价格 → 红普通也按 baseCost（无 costDiv 折扣）。
    EXPECT_NEAR(f.armyCost[0], 1.0, 1e-12);
    EXPECT_NEAR(f.armyCost[1], 3.97, 1e-12);
    EXPECT_DOUBLE_EQ(f.producedCost[0], 0.0);  // 公平调度堆累计花费初始为 0
    EXPECT_EQ(f.unitPreference[0], 2.0);       // 红：普通兵偏好 2（P11 改版）
    EXPECT_EQ(f.unitPreference[1], 1.0);       // 其余类型默认 1

    lw::Faction g;
    g.initFromDef(cfg.factions[8], cfg);  // 品红无价格修正也无偏好
    EXPECT_NEAR(g.armyCost[3], 17.0, 1e-12);
    EXPECT_NEAR(g.freeArmyChance, 0.6, 1e-12);
}

TEST(Faction, NeutralNotAlive) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    lw::Faction f;
    f.initFromDef(cfg.factions[0], cfg);
    EXPECT_FALSE(f.alive);
}

TEST(Faction, InsertAndRemoveCity) {
    SmallWorld w;
    auto& f = w.factions[1];
    EXPECT_EQ(f.cityCount, 0);

    // P13：城市注册到 Map（addCity 返回 cityId），Faction 持 cityIds 列表。
    const int c1 = w.map.addCity(1, 3, 4);
    f.insertCity(c1, w.map.city(c1).level);
    ASSERT_EQ(f.cityCount, 1);
    EXPECT_EQ(w.map.city(c1).baseX, 3);
    EXPECT_EQ(w.map.city(c1).baseY, 4);

    f.insertCity(c1, w.map.city(c1).level);  // 去重
    ASSERT_EQ(f.cityCount, 1);

    const int c2 = w.map.addCity(1, 5, 5);
    f.insertCity(c2, w.map.city(c2).level);
    ASSERT_EQ(f.cityCount, 2);

    f.removeCity(c1, w.map.city(c1).level);  // swap 删除，另一城仍在
    ASSERT_EQ(f.cityCount, 1);
    EXPECT_EQ(f.cityIds[0], c2);
}

TEST(Faction, MaxCityLevelTracksInsertAndRemoval) {
    SmallWorld w;
    auto& f = w.factions[1];
    const int low = w.map.addCity(1, 1, 1);
    const int high = w.map.addCity(4, 4, 4);
    f.insertCity(low, w.map.city(low).level);
    f.insertCity(high, w.map.city(high).level);
    EXPECT_DOUBLE_EQ(f.maxCityLevel, 4.0);

    f.removeCity(high, w.map.city(high).level);
    f.recomputeMaxCityLevel(w.map);
    EXPECT_DOUBLE_EQ(f.maxCityLevel, 1.0);
}

TEST(Faction, InsertCityRejectsInvalidId) {
    SmallWorld w;
    auto& f = w.factions[1];
    f.insertCity(-1, 0.0);  // 非城市（cityId=-1）拒绝
    EXPECT_EQ(f.cityCount, 0);
}

TEST(Faction, NeutralCannotOwnCities) {
    SmallWorld w;
    auto& f = w.factions[0];
    const int cid = w.map.addCity(1, 1, 1);
    f.insertCity(cid, w.map.city(cid).level);
    EXPECT_EQ(f.cityCount, 0);
    f.removeCity(cid, w.map.city(cid).level);
    EXPECT_EQ(f.cityCount, 0);
}

TEST(Faction, ConquerFromNeutral) {
    SmallWorld w;
    auto& f = w.factions[3];
    const int cid = w.map.addCity(1, 3, 4);
    w.map.at(3, 4).belongi = 0;
    lwtest::MockRng rng;
    lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};
    f.conquer(ctx, 3, 4);

    EXPECT_EQ(w.map.at(3, 4).belongi, 3);
    EXPECT_EQ(f.cityCount, 1);
    EXPECT_EQ(w.map.city(cid).ownerId, 3);
    EXPECT_EQ(f.landCount, 1);
    EXPECT_EQ(w.factions[0].landCount, 10 * 8 - 1);  // 中立初始=全部无主领地，征服后递减（2026-08 归零语义）
    EXPECT_TRUE(w.pending.empty());
}

TEST(Faction, ConquerIgnoresOutOfBoundsAndSameOwner) {
    SmallWorld w;
    auto& f = w.factions[2];
    const int cid = w.map.addCity(1, 1, 1);
    w.map.at(1, 1).belongi = 2;  // 已属自己

    lwtest::MockRng rng;
    lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};
    f.conquer(ctx, -1, 0);
    f.conquer(ctx, 0, 100);
    f.conquer(ctx, 10, 0);
    f.conquer(ctx, 1, 1);
    EXPECT_EQ(f.cityCount, 0);
    EXPECT_EQ(f.landCount, 0);
    EXPECT_EQ(w.map.at(1, 1).belongi, 2);
    EXPECT_EQ(w.map.city(cid).ownerId, 0);  // 仍中立
}

TEST(Faction, ConquerStealsFromOwner) {
    SmallWorld w;
    auto& owner = w.factions[5];
    auto& thief = w.factions[6];
    const int cid = w.map.addCity(1, 4, 4);
    w.map.at(4, 4).belongi = 0;
    lwtest::MockRng rng;
    {
        lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};
        owner.conquer(ctx, 4, 4);
    }
    EXPECT_EQ(w.map.at(4, 4).belongi, 5);
    EXPECT_EQ(owner.cityCount, 1);
    EXPECT_EQ(w.map.city(cid).ownerId, 5);
    EXPECT_EQ(owner.landCount, 1);

    w.map.at(4, 4).belongi = 5;
    {
        lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};
        thief.conquer(ctx, 4, 4);
    }
    EXPECT_EQ(w.map.at(4, 4).belongi, 6);
    EXPECT_EQ(owner.cityCount, 0);
    EXPECT_EQ(owner.landCount, 0);
    EXPECT_EQ(thief.cityCount, 1);
    EXPECT_EQ(w.map.city(cid).ownerId, 6);
    EXPECT_EQ(thief.landCount, 1);
}

TEST(Faction, Faction8FreeArmyOnCity) {
    SmallWorld w;
    auto& f = w.factions[8];
    const int cid = w.map.addCity(1, 2, 2);
    w.map.at(2, 2).belongi = 0;
    lwtest::MockRng rng;
    rng.results = {true};
    {
        lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};
        f.conquer(ctx, 2, 2);
    }

    ASSERT_EQ(w.pending.size(), 1u);
    EXPECT_EQ(w.pending[0].type, 0);
    EXPECT_EQ(w.pending[0].factionId, 8);
    // P13：一级城市中心 = 锚点 + 0.5（1×1 中心）。
    EXPECT_DOUBLE_EQ(w.pending[0].x, 2.5);
    EXPECT_DOUBLE_EQ(w.pending[0].y, 2.5);
    EXPECT_EQ(w.map.city(cid).ownerId, 8);
}

TEST(Faction, Faction8FreeArmyMiss) {
    SmallWorld w;
    auto& f = w.factions[8];
    const int cid = w.map.addCity(1, 2, 2);
    w.map.at(2, 2).belongi = 0;
    lwtest::MockRng rng;
    rng.results = {false};
    {
        lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};
        f.conquer(ctx, 2, 2);
    }

    EXPECT_TRUE(w.pending.empty());
    EXPECT_EQ(w.map.at(2, 2).belongi, 8);  // 征服本身仍生效
    EXPECT_EQ(w.map.city(cid).ownerId, 8);
}

// P13 整城易主：多格城市全部基建格同时属同一势力才整城易主；部分占领/第三方占格只改土地归属。
TEST(Faction, PartialOccupationDoesNotTransferCity) {
    SmallWorld w;
    auto& f3 = w.factions[3];
    auto& f4 = w.factions[4];
    const int cid = w.map.addCity(4, 1, 1);  // 4 级 = 2×2：基建格 (1,1)(2,1)(1,2)(2,2)
    w.map.at(1, 1).belongi = 0;
    w.map.at(2, 1).belongi = 0;
    w.map.at(1, 2).belongi = 0;
    w.map.at(2, 2).belongi = 0;
    lwtest::MockRng rng;
    lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};

    f3.conquer(ctx, 1, 1);  // 部分占领 1/4 格
    EXPECT_EQ(w.map.city(cid).ownerId, 0);
    EXPECT_EQ(f3.cityCount, 0);

    f4.conquer(ctx, 2, 2);  // 第三方占领 1 格（f3 仍持 1 格）
    EXPECT_EQ(w.map.city(cid).ownerId, 0);
    EXPECT_EQ(f3.cityCount, 0);
    EXPECT_EQ(f4.cityCount, 0);

    f4.conquer(ctx, 1, 2);
    f4.conquer(ctx, 2, 1);  // f4 持 3 格，f3 持 1 格 → 仍不整城易主
    EXPECT_EQ(w.map.city(cid).ownerId, 0);
    EXPECT_EQ(f4.cityCount, 0);

    f3.conquer(ctx, 2, 2);  // f3 夺回 (2,2)：f3 持 (1,1)(2,2)，f4 持 (1,2)(2,1) → 无
    EXPECT_EQ(w.map.city(cid).ownerId, 0);

    f3.conquer(ctx, 1, 2);
    f3.conquer(ctx, 2, 1);  // f3 集齐全部 4 格 → 整城易主
    EXPECT_EQ(w.map.city(cid).ownerId, 3);
    EXPECT_EQ(f3.cityCount, 1);
    EXPECT_EQ(f4.cityCount, 0);
    EXPECT_EQ(f3.landCount, 4);
    EXPECT_EQ(f4.landCount, 0);
}

// P13 整城易主只触发一次免费产兵：多格城市逐格占领不产兵，整城易主才产（中心点一次）。
TEST(Faction, Faction8FreeArmyOncePerWholeCity) {
    SmallWorld w;
    auto& f = w.factions[8];
    const int cid = w.map.addCity(4, 1, 1);  // 2×2：中心 (2.0, 2.0)
    w.map.at(1, 1).belongi = 0;
    w.map.at(2, 1).belongi = 0;
    w.map.at(1, 2).belongi = 0;
    w.map.at(2, 2).belongi = 0;
    lwtest::MockRng rng;
    rng.results = {true};
    lw::ConquerContext ctx{w.map, w.factions, rng, w.pending};

    f.conquer(ctx, 1, 1);
    f.conquer(ctx, 2, 1);
    f.conquer(ctx, 1, 2);  // 前 3 格仅改土地归属，不整城易主 → 无免费兵
    EXPECT_TRUE(w.pending.empty());

    f.conquer(ctx, 2, 2);  // 第 4 格集齐 → 整城易主 → 免费产兵一次
    ASSERT_EQ(w.pending.size(), 1u);
    EXPECT_EQ(w.pending[0].type, 0);
    EXPECT_EQ(w.pending[0].factionId, 8);
    // 2×2 城市中心 = 锚点 + (w/2, h/2) = (1+1, 1+1)。
    EXPECT_DOUBLE_EQ(w.pending[0].x, 2.0);
    EXPECT_DOUBLE_EQ(w.pending[0].y, 2.0);
    EXPECT_EQ(w.map.city(cid).ownerId, 8);
    EXPECT_EQ(f.cityCount, 1);
}

}  // namespace
