// test_config.cpp — Config 加载单测（翻新计划 §3.5）。验证默认值、JSON 覆盖与 data/config.json。
#include <gtest/gtest.h>

#include "core/Config.h"

namespace {

TEST(Config, Defaults) {
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    EXPECT_EQ(cfg.map.width, 105);
    EXPECT_EQ(cfg.map.height, 95);
    EXPECT_EQ(cfg.map.blockSize, 15);
    EXPECT_EQ(cfg.map.panelWidth, 600);
    EXPECT_NEAR(cfg.army.baseSpeed, 0.3, 1e-12);
    EXPECT_NEAR(cfg.sea.goSeaIncrease, 1.7 / 20000.0, 1e-12);
    EXPECT_EQ(cfg.units[0].cost, 1.0);
    EXPECT_EQ(cfg.units[1].cost, 3.97);
    EXPECT_EQ(cfg.units[3].speedMult, 0.6);
    // 细节改进（2026-08）：先锋 bounceMult 0.4（60% 不反弹）、开拓恒反弹 + 山地特性。
    EXPECT_EQ(cfg.units[1].bounceMult, 0.4);
    EXPECT_EQ(cfg.units[2].bounceMult, 1.0);
    EXPECT_EQ(cfg.units[2].mountainEnterMult, 1.6);
    EXPECT_EQ(cfg.units[2].mountainNoSlow, true);
    // 势力表 1..8 内置默认。
    ASSERT_EQ(cfg.factions.size(), 9u);
    EXPECT_EQ(cfg.factions[1].unitPreference[0], 2.0);   // 红：普通兵偏好 2（P11 改版）
    EXPECT_EQ(cfg.factions[3].speedMultAll, 1.5);        // 青：全速 ×1.5
    EXPECT_EQ(cfg.factions[5].extraLaserBeams, 2);       // 绿：激光 3 条
    EXPECT_EQ(cfg.factions[6].bombRadius, 4.08);         // 橙：爆炸半径覆盖
    EXPECT_EQ(cfg.effect.bomb.baseRadius, 2.4);          // 炸弹死亡爆炸基础半径
    EXPECT_EQ(cfg.factions[7].mineTriggerRadius, 1.43);  // 紫：地雷引爆爆炸半径覆盖
    EXPECT_EQ(cfg.factions[8].freeArmyChance, 0.6);      // 品红：免费产兵
    // P9 新兵种（手枪/霰弹）与 projectile 段。
    ASSERT_EQ(cfg.units.size(), 8u);
    EXPECT_EQ(cfg.units[6].cost, 80.0);       // 手枪成本 80
    EXPECT_EQ(cfg.units[6].speedMult, 0.5);   // 速度 0.5×
    EXPECT_EQ(cfg.units[6].sizeMult, 1.3);    // 碰撞半径 1.3×
    EXPECT_EQ(cfg.units[6].periodic, lw::PeriodicAction::firePistol);
    EXPECT_EQ(cfg.units[6].periodTicks, 120);
    EXPECT_EQ(cfg.units[6].bulletSpeed, 0.5);  // 2026-08-07 定为原值 1.5 的 1/3
    EXPECT_EQ(cfg.units[6].bulletLifespanTicks, 90);
    EXPECT_EQ(cfg.units[7].cost, 120.0);      // 霰弹成本 120
    EXPECT_EQ(cfg.units[7].speedMult, 0.5);
    EXPECT_EQ(cfg.units[7].sizeMult, 1.3);
    EXPECT_EQ(cfg.units[7].periodic, lw::PeriodicAction::fireShotgun);
    EXPECT_EQ(cfg.units[7].periodTicks, 180);
    EXPECT_NEAR(cfg.units[7].bulletSpeed, 1.0 / 3.0, 1e-12);        // 原值 1.0 的 1/3
    // 2026-08 细节改进：速度方差收窄到基准值 0.8~1.2 倍（±20%，原 ±50% = 0.5/3）。
    EXPECT_NEAR(cfg.units[7].bulletSpeedJitter, 0.2 / 3.0, 1e-12);
    EXPECT_EQ(cfg.units[7].bulletCount, 3);
    EXPECT_EQ(cfg.units[7].bulletLifespanTicks, 60);
    // P9（2026-08-07）散射：霰弹全角 40°（=2π/9，π 分数），单颗抖动 = 半角的 0.3。
    EXPECT_EQ(cfg.units[6].bulletSpreadPIFrac, 0.0);  // 手枪单发无散射
    EXPECT_NEAR(cfg.units[7].bulletSpreadPIFrac, 2.0 / 9.0, 1e-12);
    EXPECT_NEAR(cfg.units[7].bulletSpreadJitterFrac, 0.3, 1e-12);
    EXPECT_EQ(cfg.projectile.drawSize, 32);  // 子弹 sprite 边长（= sprite 原生）
    EXPECT_EQ(cfg.projectile.mountainLifespanPenalty, 2);
    // P14 经济重构：朴素逐 tick 收入键（占位默认）；旧魔法公式键已删除。
    EXPECT_NEAR(cfg.economy.initialEconomy, 1.1, 1e-12);
    EXPECT_NEAR(cfg.economy.perLandIncome, 0.003, 1e-12);
    EXPECT_NEAR(cfg.economy.cityBaseMult, 8.0, 1e-12);
    EXPECT_NEAR(cfg.economy.capitalBase, 0.0, 1e-12);
}

TEST(Config, JsonOverrides) {
    const char* json = R"({
        "map": { "width": 200, "height": 150, "blockSize": 20 },
        "army": { "baseSpeed": 0.5 },
        "units": [ { "type": "laser", "cost": 99.0 } ],
        "factions": [ { "id": 1, "color": [1,2,3], "unitPreference": { "normal": 2.5 } } ],
        "economy": { "initialEconomy": 5.0, "perLandIncome": 0.5, "cityBaseMult": 3.0 }
    })";
    const lw::Config cfg = lw::Config::loadFromJson(json);
    EXPECT_EQ(cfg.map.width, 200);
    EXPECT_EQ(cfg.map.height, 150);
    EXPECT_EQ(cfg.map.blockSize, 20);
    EXPECT_NEAR(cfg.army.baseSpeed, 0.5, 1e-12);
    EXPECT_NEAR(cfg.units[3].cost, 99.0, 1e-12);
    EXPECT_EQ(cfg.units[0].cost, 1.0);  // 未覆盖的保持默认
    EXPECT_EQ(cfg.factions[1].color[0], 1);
    EXPECT_EQ(cfg.factions[1].color[1], 2);
    EXPECT_EQ(cfg.factions[1].color[2], 3);
    EXPECT_EQ(cfg.factions[1].unitPreference[0], 2.5);
    EXPECT_EQ(cfg.factions[1].unitPreference[1], 1.0);  // 未覆盖的保持默认
    EXPECT_NEAR(cfg.economy.initialEconomy, 5.0, 1e-12);
    EXPECT_NEAR(cfg.economy.perLandIncome, 0.5, 1e-12);
    EXPECT_NEAR(cfg.economy.cityBaseMult, 3.0, 1e-12);
    EXPECT_NEAR(cfg.economy.capitalBase, 0.0, 1e-12);  // 未覆盖的保持默认
}

TEST(Config, LoadsDataFile) {
    // 从项目根运行（ctest 的 WORKING_DIRECTORY 已设为源码根）。
    const lw::Config cfg = lw::Config::loadFromFile("data/config.json");
    EXPECT_EQ(cfg.map.width, 105);
    EXPECT_NEAR(cfg.sea.goSeaIncrease, 0.000085, 1e-9);
    EXPECT_NEAR(cfg.sea.goSeaChanceDenominator, 17700.0, 1e-9);
    EXPECT_EQ(cfg.factions.size(), 9u);
    EXPECT_EQ(cfg.factions[1].unitPreference[0], 2.0);  // 数据文件红 normal 偏好 2
    EXPECT_NEAR(cfg.factions[3].seaMult, 0.666, 1e-9);
    EXPECT_NEAR(cfg.units[7].bulletSpreadPIFrac, 2.0 / 9.0, 1e-5);  // 数据文件 0.222222 ≈ 2/9
}

TEST(Config, MissingFileFallsBackToDefaults) {
    const lw::Config cfg = lw::Config::loadFromFile("data/no_such_config.json");
    EXPECT_EQ(cfg.map.width, 105);
    EXPECT_EQ(cfg.factions.size(), 9u);
}

}  // namespace
