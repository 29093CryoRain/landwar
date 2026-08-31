// test_buff.cpp — 增益系统（开发计划 P7）单测。
// 覆盖：computeMods 聚合（乘算/加算/stacks）、势力定义初始 buff 与 mods 等价、
// 迁移读取点（conquer 免费兵概率、快照读档重建 buffs+mods）、速度链行为保持。
#include <gtest/gtest.h>

#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "core/Simulation.h"
#include "replay/Snapshot.h"
#include "sim/Buff.h"
#include "world/Faction.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 记录 chance() 收到的概率（验证 conquer 免费兵概率已乘增益）。
class RecordingRng final : public Rng {
public:
    double lastP = -1.0;
    bool result = true;
    bool chance(double p) override {
        lastP = p;
        return result;
    }
};

TEST(Buff, NoBuffsIdentity) {
    const FactionMods m = computeMods({});
    for (int t = 0; t < kArmyTypeCount; ++t) {
        EXPECT_DOUBLE_EQ(m.costMult[static_cast<size_t>(t)], 1.0);
        EXPECT_DOUBLE_EQ(m.speedMult[static_cast<size_t>(t)], 1.0);
    }
    EXPECT_DOUBLE_EQ(m.seaChanceMult, 1.0);
    EXPECT_DOUBLE_EQ(m.bounceChanceMult, 1.0);
    EXPECT_DOUBLE_EQ(m.economyGainMult, 1.0);
    EXPECT_DOUBLE_EQ(m.explosionRadiusAdd, 1.0);
    EXPECT_DOUBLE_EQ(m.laserDurationMult, 1.0);
    EXPECT_DOUBLE_EQ(m.laserLengthMult, 1.0);
    EXPECT_DOUBLE_EQ(m.laserWidthMult, 1.0);
    EXPECT_EQ(m.laserExtraBeams, 0);
    EXPECT_DOUBLE_EQ(m.mineTimeoutMult, 1.0);
    EXPECT_DOUBLE_EQ(m.freeArmyChanceMult, 1.0);
}

TEST(Buff, GlobalSpeedMultAppliesAllTypes) {
    const FactionMods m = computeMods({{BuffType::UnitSpeedAdd, -1, 0.5, 1, "t"}});
    for (int t = 0; t < kArmyTypeCount; ++t)
        EXPECT_DOUBLE_EQ(m.speedMult[static_cast<size_t>(t)], 1.5);
}

TEST(Buff, PerTypeSpeedMultOnlyThatType) {
    const FactionMods m =
        computeMods({{BuffType::UnitSpeedAdd, static_cast<int>(ArmyType::pioneer), 1.0, 1, "t"}});
    for (int t = 0; t < kArmyTypeCount; ++t) {
        const double expect =
            (t == static_cast<int>(ArmyType::pioneer)) ? 2.0 : 1.0;
        EXPECT_DOUBLE_EQ(m.speedMult[static_cast<size_t>(t)], expect);
    }
}

// P8 聚合规则（思路"科技细节"）：数值增加默认加算、数值减小默认乘算。
TEST(Buff, StacksAppliedAdditiveForIncrease) {
    // 增幅（速度）加算：1 + 3×0.5 = 2.5。
    const FactionMods m = computeMods({{BuffType::UnitSpeedAdd, -1, 0.5, 3, "t"}});
    EXPECT_DOUBLE_EQ(m.speedMult[0], 2.5);
}

TEST(Buff, IncreaseTypesAdditive) {
    // 两条增幅加算：1 + 0.5 + 0.2 = 1.7。
    const FactionMods m = computeMods({{BuffType::EconomyGainAdd, -1, 0.5, 1, "a"},
                                       {BuffType::EconomyGainAdd, -1, 0.2, 1, "b"}});
    EXPECT_DOUBLE_EQ(m.economyGainMult, 1.7);
}

TEST(Buff, DecreaseTypesMultiplicative) {
    // 减幅（造价）乘算：0.9 × 0.8 = 0.72（规则不变）。
    const FactionMods m = computeMods({{BuffType::UnitCostMult, 0, 0.9, 1, "a"},
                                       {BuffType::UnitCostMult, 0, 0.8, 1, "b"}});
    EXPECT_DOUBLE_EQ(m.costMult[0], 0.72);
    EXPECT_DOUBLE_EQ(m.costMult[1], 1.0);  // 其他兵种不受影响
}

TEST(Buff, ExplosionRadiusBonusesAreScoped) {
    const FactionMods bomb = computeMods(
        {{BuffType::BombExplosionRadiusAdd, static_cast<int>(ArmyType::bomb), 0.5, 1, "faction"}});
    EXPECT_DOUBLE_EQ(bomb.bombExplosionRadiusAdd, 1.5);
    EXPECT_DOUBLE_EQ(bomb.mineExplosionRadiusAdd, 1.0);

    const FactionMods mine = computeMods(
        {{BuffType::MineExplosionRadiusAdd, static_cast<int>(ArmyType::mine), 0.5, 1, "faction"}});
    EXPECT_DOUBLE_EQ(mine.bombExplosionRadiusAdd, 1.0);
    EXPECT_DOUBLE_EQ(mine.mineExplosionRadiusAdd, 1.5);
}

TEST(Buff, CountTypesSum) {
    const FactionMods m = computeMods({{BuffType::LaserExtraBeams, -1, 1.0, 1, "a"},
                                       {BuffType::LaserExtraBeams, -1, 2.0, 1, "b"},
                                       {BuffType::ProjectileCountExtra, static_cast<int>(ArmyType::shotgun), 2.0, 1, "c"}});
    EXPECT_EQ(m.laserExtraBeams, 3);
    EXPECT_EQ(m.projectileCountExtra[static_cast<size_t>(ArmyType::shotgun)], 2);
    EXPECT_EQ(m.projectileCountExtra[static_cast<size_t>(ArmyType::normal)], 0);
}

TEST(Buff, ActionRateAndTechGainDefaults) {
    const FactionMods m = computeMods({});
    EXPECT_DOUBLE_EQ(m.actionRateMult[0], 1.0);
    EXPECT_DOUBLE_EQ(m.techGainMult, 1.0);
}

// 迁移核心：势力定义初始 buff 聚合出的 mods == 迁移前的原字段值。
TEST(Buff, InitialFactionBuffsReproduceDefinitions) {
    const Config cfg = Config::loadFromJson("{}");
    const auto expectIdentity = [](const FactionMods& m) {
        for (int t = 0; t < kArmyTypeCount; ++t) {
            EXPECT_DOUBLE_EQ(m.speedMult[static_cast<size_t>(t)], 1.0);
            EXPECT_DOUBLE_EQ(m.costMult[static_cast<size_t>(t)], 1.0);
        }
        EXPECT_DOUBLE_EQ(m.seaChanceMult, 1.0);
        EXPECT_DOUBLE_EQ(m.bounceChanceMult, 1.0);
        EXPECT_EQ(m.laserExtraBeams, 0);
        EXPECT_DOUBLE_EQ(m.laserDurationMult, 1.0);
        EXPECT_DOUBLE_EQ(m.laserLengthMult, 1.0);
    };

    // 青3：全兵速 ×1.5、下海概率 ×0.666（原 speedMultAll / seaMult）。
    {
        const auto m = computeMods(initialFactionBuffs(cfg.factions[3]));
        for (int t = 0; t < kArmyTypeCount; ++t)
            EXPECT_DOUBLE_EQ(m.speedMult[static_cast<size_t>(t)], 1.5);
        EXPECT_DOUBLE_EQ(m.seaChanceMult, 0.666);
    }
    // 蓝4：开拓速 ×2、全兵反弹概率 ×0.6（原 pioneerSpeedMult / bounceMultAll）。
    {
        const auto m = computeMods(initialFactionBuffs(cfg.factions[4]));
        EXPECT_DOUBLE_EQ(m.speedMult[static_cast<size_t>(ArmyType::pioneer)], 2.0);
        for (int t = 0; t < kArmyTypeCount; ++t) {
            if (t != static_cast<int>(ArmyType::pioneer)) {
                EXPECT_DOUBLE_EQ(m.speedMult[static_cast<size_t>(t)], 1.0);
            }
        }
        EXPECT_DOUBLE_EQ(m.bounceChanceMult, 0.6);
    }
    // 绿5：额外激光 +2、激光寿命 ×1.5、长度 ×1.5（原 extraLaserBeams / laserDurationMult / lengthMult）。
    {
        const auto m = computeMods(initialFactionBuffs(cfg.factions[5]));
        EXPECT_EQ(m.laserExtraBeams, 2);
        EXPECT_DOUBLE_EQ(m.laserDurationMult, 1.5);
        EXPECT_DOUBLE_EQ(m.laserLengthMult, 1.5);
    }
    // 其余势力无修饰符 → mods 全恒等。
    for (int id : {1, 2, 6, 7, 8})
        expectIdentity(computeMods(initialFactionBuffs(cfg.factions[id])));
}

TEST(Buff, FactionInitFromDefPopulatesBuffsAndMods) {
    const Config cfg = Config::loadFromJson("{}");
    Faction f;
    f.initFromDef(cfg.factions[3], cfg);
    ASSERT_EQ(f.buffs.size(), 2u);  // 青：全兵速 + 下海概率
    EXPECT_EQ(f.buffs[0].source, "faction");
    EXPECT_DOUBLE_EQ(f.mods.speedMult[0], 1.5);
    EXPECT_DOUBLE_EQ(f.mods.seaChanceMult, 0.666);
}

TEST(Buff, RecomputeModsAfterBuffChange) {
    const Config cfg = Config::loadFromJson("{}");
    Faction f;
    f.initFromDef(cfg.factions[1], cfg);  // 红无修饰符
    EXPECT_DOUBLE_EQ(f.mods.speedMult[0], 1.0);
    f.buffs.push_back({BuffType::UnitSpeedAdd, -1, 0.2, 1, "tech:t"});
    f.recomputeMods();
    EXPECT_DOUBLE_EQ(f.mods.speedMult[0], 1.2);
}

TEST(Buff, ConquerUsesFreeArmyChanceMult) {
    // 势力8 免费兵概率 = 定义基数（0.6）× 增益乘数（0.5）→ chance 收到 0.3。
    Config cfg = Config::loadFromJson("{}");
    Config::Map mcfg;
    mcfg.width = 10;
    mcfg.height = 8;
    Map map;
    map.configure(mcfg);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 10; ++x) map.at(x, y).land = true;

    std::vector<Faction> factions(static_cast<size_t>(kFactionTotal));
    for (int id = 0; id < kFactionTotal; ++id)
        factions[static_cast<size_t>(id)].initFromDef(cfg.factions[static_cast<size_t>(id)], cfg);
    factions[8].buffs.push_back({BuffType::FreeArmyChanceMult, -1, 0.5, 1, "t"});
    factions[8].recomputeMods();

    map.addCity(1, 2, 2);  // (2,2) 成为城市（cid 无需引用，征服行为即验证目标）
    map.at(2, 2).belongi = 0;
    RecordingRng rng;
    std::vector<PendingSpawn> pending;
    ConquerContext ctx{map, factions, rng, pending};
    factions[8].conquer(ctx, 2, 2);
    EXPECT_DOUBLE_EQ(rng.lastP, 0.3);  // 0.6 × 0.5
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].type, static_cast<int>(ArmyType::normal));
}

// 读档路径：Snapshot 不序列化 buffs，读档后必须从定义重建（mods 与直跑一致）。
TEST(Buff, SnapshotReloadRebuildsBuffsAndMods) {
    Simulation sim(lwtest::loadCfg(), 7u);
    ASSERT_TRUE(sim.init());
    const std::string json = Snapshot::serialize(sim);

    Simulation loaded;
    ASSERT_TRUE(Snapshot::deserialize(loaded, json));
    // 青3 的 mods 在读档后恢复（buffs 从 config 定义重建）。
    EXPECT_DOUBLE_EQ(loaded.faction(3).mods.speedMult[0], 1.2);
    EXPECT_DOUBLE_EQ(loaded.faction(3).mods.seaChanceMult, 0.666);
    EXPECT_DOUBLE_EQ(loaded.faction(5).mods.laserDurationMult, 1.5);
    EXPECT_EQ(loaded.faction(5).mods.laserExtraBeams, 2);
    EXPECT_EQ(loaded.faction(3).buffs.size(), 2u);
    // 蓝4 开拓速度仍在。
    EXPECT_DOUBLE_EQ(loaded.faction(4).mods.speedMult[static_cast<size_t>(ArmyType::pioneer)], 1.5);
}

}  // namespace
