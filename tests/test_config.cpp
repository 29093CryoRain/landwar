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
    // 双色系统（⑫）：副色默认浅灰 191；中立灰占位主色 = 深灰 96（用户定夺"深灰+浅灰"）。
    EXPECT_EQ(cfg.factions[0].color, (std::array<int, 3>{96, 96, 96}));
    for (int id = 0; id < static_cast<int>(cfg.factions.size()); ++id)
        EXPECT_EQ(cfg.factions[static_cast<size_t>(id)].secondary,
                  (std::array<int, 3>{191, 191, 191}));
    // 双色混合比例默认（render.tile 主:副:白、render.city.mix 主:副:黑）。
    EXPECT_NEAR(cfg.render.tileMix.primary, 0.35, 1e-12);
    EXPECT_NEAR(cfg.render.tileMix.secondary, 0.35, 1e-12);
    EXPECT_NEAR(cfg.render.tileMix.white, 0.30, 1e-12);
    EXPECT_NEAR(cfg.render.city.mix.primary, 0.60, 1e-12);
    EXPECT_NEAR(cfg.render.city.mix.secondary, 0.20, 1e-12);
    EXPECT_NEAR(cfg.render.city.mix.black, 0.20, 1e-12);
    // 旧死键 cyanSeaMult 已删除（势力3 下海概率 ×0.666 由 factions[3].seaMult 承担）。
    EXPECT_NEAR(cfg.factions[3].seaMult, 0.666, 1e-9);
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
        "factions": [ { "id": 1, "color": [1,2,3], "secondary": [4,5,6],
                        "unitPreference": { "normal": 2.5 } } ],
        "render": { "tile": { "primary": 0.4, "secondary": 0.3, "white": 0.3 },
                    "city": { "mix": { "primary": 0.7, "secondary": 0.1, "black": 0.2 } } },
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
    EXPECT_EQ(cfg.factions[1].secondary, (std::array<int, 3>{4, 5, 6}));  // 副色覆盖
    EXPECT_EQ(cfg.factions[1].unitPreference[0], 2.5);
    EXPECT_EQ(cfg.factions[1].unitPreference[1], 1.0);  // 未覆盖的保持默认
    EXPECT_NEAR(cfg.render.tileMix.primary, 0.4, 1e-12);
    EXPECT_NEAR(cfg.render.city.mix.primary, 0.7, 1e-12);
    EXPECT_NEAR(cfg.economy.initialEconomy, 5.0, 1e-12);
    EXPECT_NEAR(cfg.economy.perLandIncome, 0.5, 1e-12);
    EXPECT_NEAR(cfg.economy.cityBaseMult, 3.0, 1e-12);
    EXPECT_NEAR(cfg.economy.capitalBase, 0.0, 1e-12);  // 未覆盖的保持默认
}

TEST(Config, TwoToneDerivedColors) {
    // 双色派生色公式（⑫）：地块格 = 主:副:白、城市图标 = 主:副:黑（加权平均四舍五入）。
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    // 红势力（主 255,0,0 / 副 191）：tile = 0.35·255+0.35·191+0.30·255=232.6→233，
    // g/b = 0.35·0+0.35·191+0.30·255=143.35→143。
    EXPECT_EQ(cfg.factionTileColor(1), (std::array<int, 3>{233, 143, 143}));
    // 城市图标 = 0.6·255+0.2·191=191.2→191、g = 0.2·191=38.2→38。
    EXPECT_EQ(cfg.factionCityColor(1), (std::array<int, 3>{191, 38, 38}));
    // 中立（深灰 96 + 浅灰）：tile → 0.35·96+0.35·191+0.30·255=176.95→177；city → 95.8→96。
    EXPECT_EQ(cfg.factionTileColor(0), (std::array<int, 3>{177, 177, 177}));
    EXPECT_EQ(cfg.factionCityColor(0), (std::array<int, 3>{96, 96, 96}));
    // 越界 → 中性灰占位（不崩）。
    EXPECT_EQ(cfg.factionTileColor(99), (std::array<int, 3>{177, 177, 177}));
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
    // 双色系统（⑫）：数据文件 id0 = 深灰主色 + 浅灰副色；id1 副色 = 浅灰。
    EXPECT_EQ(cfg.factions[0].color, (std::array<int, 3>{96, 96, 96}));
    EXPECT_EQ(cfg.factions[0].secondary, (std::array<int, 3>{191, 191, 191}));
    EXPECT_EQ(cfg.factions[1].secondary, (std::array<int, 3>{191, 191, 191}));
    EXPECT_NEAR(cfg.render.tileMix.primary, 0.35, 1e-9);
    EXPECT_NEAR(cfg.render.city.mix.primary, 0.6, 1e-9);
    EXPECT_NEAR(cfg.units[7].bulletSpreadPIFrac, 2.0 / 9.0, 1e-5);  // 数据文件 0.222222 ≈ 2/9
}

TEST(Config, MissingFileFallsBackToDefaults) {
    const lw::Config cfg = lw::Config::loadFromFile("data/no_such_config.json");
    EXPECT_EQ(cfg.map.width, 105);
    EXPECT_EQ(cfg.factions.size(), 9u);
}

}  // namespace
