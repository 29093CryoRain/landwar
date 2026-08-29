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
    EXPECT_NEAR(cfg.army.bounceJitterRangeRad, 0.03, 1e-12);
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
        "map": { "blockSize": 20 },
        "army": { "baseSpeed": 0.5, "bounceJitterRangeRad": 0.07 },
        "units": [ { "type": "laser", "cost": 99.0 } ],
        "factions": [ { "id": 1, "color": [1,2,3], "secondary": [4,5,6],
                        "unitPreference": { "normal": 2.5 } } ],
        "render": { "tile": { "primary": 0.4, "secondary": 0.3, "white": 0.3 },
                    "city": { "mix": { "primary": 0.7, "secondary": 0.1, "black": 0.2 } } },
        "economy": { "initialEconomy": 5.0, "perLandIncome": 0.5, "cityBaseMult": 3.0 }
    })";
    const lw::Config cfg = lw::Config::loadFromJson(json);
    EXPECT_EQ(cfg.map.blockSize, 20);
    EXPECT_NEAR(cfg.army.baseSpeed, 0.5, 1e-12);
    EXPECT_NEAR(cfg.army.bounceJitterRangeRad, 0.07, 1e-12);
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

TEST(Config, TileVariationGradient) {
    // 双色密铺分档（2026-08）：factionTileColor(id, t) 沿 variation 在副色侧/主色侧渐变。
    // 默认 TileMix = {0.35, 0.35, 0.30}, variation = 0.1 → t=0 权重 (0.25,0.45,0.30)、
    // t=0.5 (0.35,0.35,0.30)、t=1 (0.45,0.25,0.30)。红势力（主 255,0,0 / 副 191）。
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    // t=0.5 = 中点 = factionTileColor(id)。
    EXPECT_EQ(cfg.factionTileColor(1, 0.5), cfg.factionTileColor(1));
    // t=0（副色侧）：r = 0.25·255+0.45·191+0.30·255 = 226.2→226；g/b = 0.45·191+0.30·255 = 162.45→162。
    EXPECT_EQ(cfg.factionTileColor(1, 0.0), (std::array<int, 3>{226, 162, 162}));
    // t=1（主色侧）：r = 0.45·255+0.25·191+0.30·255 = 239.0→239；g/b = 0.25·191+0.30·255 = 124.25→124。
    EXPECT_EQ(cfg.factionTileColor(1, 1.0), (std::array<int, 3>{239, 124, 124}));
    // 单调：t 越大主权重越大 → r 应随 t 递增（通道：红通道主色分量高）。
    EXPECT_LT(cfg.factionTileColor(1, 0.0)[0], cfg.factionTileColor(1, 0.5)[0]);
    EXPECT_LT(cfg.factionTileColor(1, 0.5)[0], cfg.factionTileColor(1, 1.0)[0]);
    // 中立（96 主 / 191 副，id0）：t=0 → (186,186,186)；t=1 → (167,167,167)（对角单调递减）。
    EXPECT_EQ(cfg.factionTileColor(0, 0.0), (std::array<int, 3>{186, 186, 186}));
    EXPECT_EQ(cfg.factionTileColor(0, 1.0), (std::array<int, 3>{167, 167, 167}));
    // 越界用中性灰占位，不崩。
    EXPECT_EQ(cfg.factionTileColor(99, 0.0), (std::array<int, 3>{186, 186, 186}));
}

TEST(Config, LoadsDataFile) {
    // 从项目根运行（ctest 的 WORKING_DIRECTORY 已设为源码根）。
    const lw::Config cfg = lw::Config::loadFromFile("data/config.json");
    EXPECT_EQ(cfg.map.width, 105);
    EXPECT_NEAR(cfg.army.bounceJitterRangeRad, 0.03, 1e-12);
    EXPECT_NEAR(cfg.sea.goSeaIncrease, 0.000085, 1e-9);
    EXPECT_NEAR(cfg.sea.goSeaChanceDenominator, 17700.0, 1e-9);
    EXPECT_EQ(cfg.factions.size(), 9u);
    EXPECT_EQ(cfg.tech.techs.size(), 21u);  // techs.json 已合并
    EXPECT_EQ(cfg.factions[1].unitPreference[0], 2.0);  // 数据文件红 normal 偏好 2
    EXPECT_NEAR(cfg.factions[3].seaMult, 0.666, 1e-9);
    // 双色系统（⑫）：数据文件 id0 = 深灰主色 + 浅灰副色；id1 副色 = 浅灰。
    EXPECT_EQ(cfg.factions[0].color, (std::array<int, 3>{96, 96, 96}));
    EXPECT_EQ(cfg.factions[0].secondary, (std::array<int, 3>{191, 191, 191}));
    EXPECT_EQ(cfg.factions[1].secondary, (std::array<int, 3>{191, 191, 191}));
    EXPECT_NEAR(cfg.render.tileMix.primary, 0.5, 1e-9);   // 数据文件（用户 2026-08 调值：主0.5/副0.3/白0.2）
    EXPECT_NEAR(cfg.render.city.mix.primary, 0.7, 1e-9);  // 数据文件（用户调值：主0.7/副0.1/黑0.2）
    EXPECT_NEAR(cfg.units[7].bulletSpreadPIFrac, 2.0 / 9.0, 1e-5);  // 数据文件 0.222222 ≈ 2/9
}

TEST(Config, MissingFileFallsBackToDefaults) {
    const lw::Config cfg = lw::Config::loadFromFile("data/no_such_config.json");
    EXPECT_EQ(cfg.map.width, 105);
    EXPECT_EQ(cfg.factions.size(), 9u);
}

TEST(Config, InvalidNumericValuesFallBackToDefaults) {
    const lw::Config cfg = lw::Config::loadFromJson(R"({
        "effect": { "bomb": { "conquerEveryTicks": 0 } },
        "sim": { "tickRate": 0 }
    })");
    EXPECT_EQ(cfg.effect.bomb.conquerEveryTicks, 2);
    EXPECT_DOUBLE_EQ(cfg.sim.tickRate, 60.0);
}

TEST(Config, InvalidTypesFallBackToDefaults) {
    const lw::Config cfg = lw::Config::loadFromJson(R"({
        "units": [{ "type": "normal", "cost": "not-a-number" }],
        "factions": [{ "id": 1, "color": [1, "bad", 3] }]
    })");
    EXPECT_DOUBLE_EQ(cfg.units[0].cost, 1.0);
    EXPECT_EQ(cfg.factions[1].color, (std::array<int, 3>{255, 0, 0}));
}

TEST(Config, ValidateReportsInvalidRuntimeState) {
    lw::Config cfg = lw::Config::loadFromJson("{}");
    cfg.sim.tickRate = 0.0;
    std::string err;
    EXPECT_FALSE(cfg.validate(&err));
    EXPECT_NE(err.find("simulation"), std::string::npos);
}

TEST(Config, NewTilingCitySetsRoundTrip) {
    // 2026-08-16：city.tilings.<tilingName> 段（半正/Laves 形状表）JSON 往返无损。
    // 2026-08-26：ShapeCell 去 orient（运行时从不读取）；掩码序列化改 anchorBases 数组。
    lw::Config cfg = lw::Config::loadFromJson("{}");
    auto& set = cfg.city.sets[static_cast<size_t>(static_cast<int>(lw::TilingType::Arch3636))];
    set.levels = {2.25, 5.25, 8.25};
    set.shapes.resize(3);
    set.shapes[0].cells = {{0.0, 0.0}};
    set.shapes[1].cells = {{0.0, 0.0}, {1.5, 0.0}};
    set.shapes[2].cells = {{0.0, 0.0}, {1.5, 0.0}, {0.0, 1.5}};
    set.shapes[2].anchorBaseMask = (1u << 3) | (1u << 7);  // → anchorBases [3,7]

    const std::string json = cfg.toJson();
    ASSERT_NE(json.find("arch_3636"), std::string::npos);  // 新段已写入
    ASSERT_NE(json.find("anchorBases"), std::string::npos);  // 掩码以 anchorBases 数组序列化
    const lw::Config back = lw::Config::loadFromJson(json);
    const auto& backSet =
        back.city.sets[static_cast<size_t>(static_cast<int>(lw::TilingType::Arch3636))];
    ASSERT_EQ(backSet.levels.size(), 3u);
    EXPECT_NEAR(backSet.levels[0], 2.25, 1e-12);
    EXPECT_NEAR(backSet.levels[2], 8.25, 1e-12);
    ASSERT_EQ(backSet.shapes.size(), 3u);
    ASSERT_EQ(backSet.shapes[2].cells.size(), 3u);
    EXPECT_NEAR(backSet.shapes[2].cells[1].dx, 1.5, 1e-12);
    EXPECT_NEAR(backSet.shapes[2].cells[2].dy, 1.5, 1e-12);
    EXPECT_EQ(backSet.shapes[2].anchorBaseMask, (1u << 3) | (1u << 7));
}

TEST(Config, CityShapesFileProvidesSquareHexTri) {
    // P1.2：square/hex/tri 形状表从 data/city_shapes.json 加载，cells 为世界偏移。
    const lw::Config cfg = lw::Config::loadFromJson("{}");
    ASSERT_EQ(cfg.city.square.levels.size(), 5u);
    ASSERT_EQ(cfg.city.square.shapes.size(), 5u);
    ASSERT_GE(cfg.city.square.shapes[1].cells.size(), 2u);
    EXPECT_NEAR(cfg.city.square.shapes[1].cells[1].dx, 0.0, 1e-12);
    EXPECT_NEAR(cfg.city.square.shapes[1].cells[1].dy, 1.0, 1e-12);  // 1×2：第二格在下方

    ASSERT_EQ(cfg.city.hex.levels.size(), 6u);
    ASSERT_EQ(cfg.city.hex.shapes.size(), 6u);
    ASSERT_GE(cfg.city.hex.shapes[1].cells.size(), 3u);
    EXPECT_NEAR(cfg.city.hex.shapes[1].cells[1].dx, -0.5372849659117709, 1e-12);
    EXPECT_NEAR(cfg.city.hex.shapes[1].cells[1].dy, -0.9306048591020997, 1e-12);

    ASSERT_EQ(cfg.city.tri.levels.size(), 5u);
    ASSERT_EQ(cfg.city.tri.shapes.size(), 5u);
    ASSERT_GE(cfg.city.tri.shapes[1].cells.size(), 2u);
    EXPECT_NEAR(cfg.city.tri.shapes[1].cells[1].dx, 0.0, 1e-12);
    EXPECT_NEAR(cfg.city.tri.shapes[1].cells[1].dy, -0.8773826753016616, 1e-12);
}


}  // namespace
