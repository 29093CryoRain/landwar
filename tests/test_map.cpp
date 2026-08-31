// test_map.cpp — 地图解析/首都单测（翻新计划 §2.1 / Phase 2；P5 改版地形基图）。
// GoldenMap 系列与 tools/gen_golden_map.py 的黄金数据逐位对比（seed 42，锁死解析器与 RNG 序列）。
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#include "core/Config.h"
#include "core/MathUtil.h"
#include "core/Simulation.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

constexpr std::uint32_t kGoldenSeed = 42;

nlohmann::json loadGolden() {
    std::ifstream ifs("tests/data/golden_map.json");
    EXPECT_TRUE(ifs.is_open()) << "golden data not found; run: python tools/gen_golden_map.py";
    nlohmann::json j;
    ifs >> j;
    return j;
}

lw::Config loadCfg() {
    return lw::Config::loadFromFile("data/config.jsonc");
}

// 读 24bit BMP 原始像素（r,g,b 数组，row-major 自底向上，j 行 = 图像底部行）。
// 供等价性测试比较转换前后地图（不依赖 C++ Map 解析）。
std::vector<std::array<int, 3>> readBmpPixels(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    EXPECT_TRUE(f != nullptr) << "cannot open " << path;
    if (!f) return {};
    unsigned char hdr[54];
    if (std::fread(hdr, 1, 54, f) != 54) {
        ADD_FAILURE() << "header truncated: " << path;
        std::fclose(f);
        return {};
    }
    const int w = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24);
    const int h = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24);
    const int rowsize = (w * 3 + 3) & ~3;  // 标准 4 字节行对齐
    std::vector<std::array<int, 3>> px;
    px.reserve(static_cast<std::size_t>(w) * h);
    std::vector<unsigned char> row(static_cast<std::size_t>(rowsize));
    for (int j = 0; j < h; ++j) {
        if (std::fread(row.data(), 1, static_cast<std::size_t>(rowsize), f) !=
            static_cast<std::size_t>(rowsize)) {
            ADD_FAILURE() << "row " << j << " truncated: " << path;
            std::fclose(f);
            return {};
        }
        for (int i = 0; i < w; ++i) {
            px.push_back({row[i * 3 + 2], row[i * 3 + 1], row[i * 3]});  // r,g,b
        }
    }
    std::fclose(f);
    return px;
}

// 与 Map::loadFromBmp 一致的 ramp：p = clamp((x-128)/127, 0, 1)。
double ramp(int x) {
    if (x <= 128) return 0.0;
    return std::min(1.0, static_cast<double>(x - 128) / 127.0);
}

TEST(Map, GoldenLandMountainCityMatch) {
    const nlohmann::json golden = loadGolden();
    lw::Simulation sim(loadCfg(), kGoldenSeed);
    ASSERT_TRUE(sim.init());
    const auto& map = sim.map();
    ASSERT_EQ(map.width(), 105);
    ASSERT_EQ(map.height(), 95);

    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            const auto& c = map.at(x, y);
            EXPECT_EQ(c.land, golden["land"][y][x].get<bool>()) << "cell (" << x << "," << y << ") land";
            EXPECT_EQ(c.mountain, golden["mountain"][y][x].get<bool>())
                << "cell (" << x << "," << y << ") mountain";
            // P13：cityId>=0 = 格 ∈ 某城市（多格形状），对应黄金数据的逐格 city bool。
            EXPECT_EQ(c.cityId >= 0, golden["city"][y][x].get<bool>())
                << "cell (" << x << "," << y << ") city";
            // P13：逐格校验所属城市等级（非城 0），防止等级桶偏移（2026-08-07 修复过一例）。
            const int expectLevel = golden["citylevel"][y][x].get<int>();
            const int actualLevel = (c.cityId >= 0) ? map.city(c.cityId).level : 0;
            EXPECT_EQ(actualLevel, expectLevel)
                << "cell (" << x << "," << y << ") city level";
        }
    }
}

TEST(Map, GoldenCapitalsAndTotalCities) {
    const nlohmann::json golden = loadGolden();
    lw::Simulation sim(loadCfg(), kGoldenSeed);
    ASSERT_TRUE(sim.init());
    const auto& map = sim.map();

    ASSERT_EQ(map.capitalCount(), static_cast<int>(golden["capitals"].size()));
    for (int i = 0; i < map.capitalCount(); ++i) {
        const int x = golden["capitals"][i][0].get<int>();
        const int y = golden["capitals"][i][1].get<int>();
        EXPECT_EQ(map.capitalX(i), x) << "capital " << i;
        EXPECT_EQ(map.capitalY(i), y) << "capital " << i;
        // P5 改版：首都只落在"可产城"格（基图允许成城）。
        EXPECT_TRUE(golden["cityallowed"][y][x].get<bool>()) << "capital " << i
                                                             << " on no-city cell";
    }
    EXPECT_EQ(map.totalCities(), golden["total_cities"].get<int>());
}

TEST(Map, CapitalsOnLandAndMutuallySeparated) {
    lw::Simulation sim(loadCfg(), 12345);
    ASSERT_TRUE(sim.init());
    const auto& map = sim.map();
    ASSERT_EQ(map.capitalCount(), 8);
    for (int i = 0; i < map.capitalCount(); ++i) {
        const int x = map.capitalX(i);
        const int y = map.capitalY(i);
        EXPECT_TRUE(map.at(x, y).land) << "capital " << i;
        EXPECT_TRUE(map.at(x, y).cityId >= 0) << "capital " << i;
        for (int j = 0; j < i; ++j) {
            EXPECT_GE(lw::math::distance(x, y, map.capitalX(j), map.capitalY(j)),
                      static_cast<double>(loadCfg().map.capitalMinDistance))
                << "capitals " << i << " vs " << j;
        }
    }
}

TEST(Map, SeaCellsHaveNoCities) {
    lw::Simulation sim(loadCfg(), 7);
    ASSERT_TRUE(sim.init());
    const auto& map = sim.map();
    int landCells = 0, seaCells = 0;
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            if (map.at(x, y).cityId >= 0) {
                EXPECT_TRUE(map.at(x, y).land) << "sea city at (" << x << "," << y << ")";
            }
            if (map.at(x, y).land)
                ++landCells;
            else
                ++seaCells;
        }
    }
    EXPECT_GT(landCells, 0);
    EXPECT_GT(seaCells, 0);
    EXPECT_GE(map.totalCities(), 8);
}

// P5 改版：海/陆由像素颜色确定性决定（任一分量 <32 → 海），与种子无关。
TEST(Map, SeaLandDeterministicAcrossSeeds) {
    const lw::Config cfg = loadCfg();
    lw::Simulation s1(cfg, 1), s2(cfg, 7);
    ASSERT_TRUE(s1.init());
    ASSERT_TRUE(s2.init());
    for (int y = 0; y < s1.map().height(); ++y) {
        for (int x = 0; x < s1.map().width(); ++x) {
            EXPECT_EQ(s1.map().at(x, y).land, s2.map().at(x, y).land)
                << "(" << x << "," << y << ")";
        }
    }
}

// P5 改版：山标记（r=255 → p=1）确定性；城概率（g>128）种子相关 → 不同种子城市布局不同。
TEST(Map, DeterministicMountainProbabilisticCity) {
    // 自包含基图：全部陆地 + 3 个山标记 + 一批可产城格（g=200 → 城概率≈0.57）。
    const std::string path = "build/_dt_mtn_city.bmp";
    std::vector<std::pair<int, int>> zones;
    for (int x = 5; x < 100; x += 6)
        for (int y = 5; y < 90; y += 6) zones.emplace_back(x, y);
    lwtest::writeTestMapBmp(path, {{50, 50}, {30, 80}, {80, 30}}, zones);

    lw::Config cfg = loadCfg();
    cfg.map.file = path;
    lw::Simulation s1(cfg, 1), s2(cfg, 2);
    ASSERT_TRUE(s1.init());
    ASSERT_TRUE(s2.init());
    int landDiff = 0, mtnDiff = 0, cityDiff = 0, mtnCount = 0;
    for (int y = 0; y < s1.map().height(); ++y) {
        for (int x = 0; x < s1.map().width(); ++x) {
            const auto& a = s1.map().at(x, y);
            const auto& b = s2.map().at(x, y);
            if (a.land != b.land) ++landDiff;
            if (a.mountain != b.mountain) ++mtnDiff;
            if ((a.cityId >= 0) != (b.cityId >= 0)) ++cityDiff;
            if (a.mountain) ++mtnCount;
        }
    }
    EXPECT_EQ(landDiff, 0);
    EXPECT_EQ(mtnDiff, 0);            // r=255 → 确定性山
    EXPECT_GT(mtnCount, 0);           // 确有山
    EXPECT_GT(cityDiff, 0);           // 城概率性 → 不同种子城市不同
}

// P5 改版修复：首都只落在"可产城"格（基图允许成城），不会在"必然无城"格上强行放城市。
TEST(Map, CapitalsPreferCityAllowedCells) {
    const std::string path = "build/_cap_ok.bmp";
    std::vector<std::pair<int, int>> zones;
    for (int x = 5; x < 100; x += 6)
        for (int y = 5; y < 90; y += 6) zones.emplace_back(x, y);  // ~150 可产城格
    lwtest::writeTestMapBmp(path, {}, zones);
    lw::Config cfg = loadCfg();
    cfg.map.file = path;
    lw::Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    ASSERT_EQ(sim.map().capitalCount(), 8);
    for (int i = 0; i < sim.map().capitalCount(); ++i) {
        const int x = sim.map().capitalX(i), y = sim.map().capitalY(i);
        EXPECT_TRUE(sim.map().at(x, y).cityAllowed)
            << "capital " << i << " at (" << x << "," << y << ") on no-city cell";
    }
}

// 地图完全没有可产城格时 → 首都回退到任意陆地，游戏仍可运行（8 首都）。
TEST(Map, CapitalsFallbackWhenNoCityAllowed) {
    const std::string path = "build/_cap_none.bmp";
    lwtest::writeTestMapBmp(path, {}, {});  // 全普通陆（g=128 → 无可产城格）
    lw::Config cfg = loadCfg();
    cfg.map.file = path;
    lw::Simulation sim(cfg, 7);
    ASSERT_TRUE(sim.init());
    EXPECT_EQ(sim.map().capitalCount(), 8);
}

// 等价性：转换后的 map_bigIslands 逐陆格 ramp(g) ≈ 旧 cityChanceFor(亮度)（字节量化误差内）。
TEST(Map, ConvertedMapPreservesOldCityProbability) {
    const auto legacy = readBmpPixels("data/legacy_old_encoding/map_bigIslands.bmp");
    const auto conv = readBmpPixels("data/map_bigIslands.bmp");
    ASSERT_EQ(legacy.size(), conv.size());
    ASSERT_EQ(legacy.size(), 105u * 95u);
    double maxErr = 0.0;
    int landCells = 0;
    for (std::size_t idx = 0; idx < legacy.size(); ++idx) {
        const auto& oldPx = legacy[idx];
        const auto& newPx = conv[idx];
        // 海 → 海
        if (oldPx[0] == 0 && oldPx[1] == 0 && oldPx[2] == 0) {
            EXPECT_EQ(newPx, (std::array<int, 3>{0, 0, 0})) << "cell " << idx << " sea";
            continue;
        }
        ++landCells;
        // 旧山（亮度∈[1,89]）→ r=255（100% 山），新图亦应如此编码
        const int oldBright = (oldPx[0] + oldPx[1] + oldPx[2]) / 3;
        if (oldBright <= 89) {
            EXPECT_EQ(newPx[0], 255) << "cell " << idx << " old-mountain";
            continue;
        }
        // 旧城市概率（>99）→ ramp(newG) ≈ 旧公式
        const double oldChance = (oldBright > 99)
                                     ? std::pow(1.040909, oldBright - 100) / 500.0
                                     : 0.0;
        const double newProb = ramp(newPx[1]);  // g 通道
        const double err = std::fabs(newProb - std::min(1.0, oldChance));
        maxErr = std::max(maxErr, err);
        EXPECT_LE(err, 0.005) << "cell " << idx << " city-prob drift";
    }
    EXPECT_GT(landCells, 1000);
    (void)maxErr;  // 仅供调试；字节量化上限 ≈ 0.5/127 ≈ 0.004
}

}  // namespace
