// test_city.cpp — P13 城市系统单测（多格基建地块 + 等级幂律采样 + 放置回退 + 确定性 + 幂律分布）。
// 覆盖开发计划 P13 测试计划：形状推导（level→w/h→基建格集合）、等级采样各分支（mock 固定 u）、
// 放置合法/越界/重叠/锚点不可成城→回退 1 级、同 mapSeed 逐格一致、幂律分布（容差断言）。
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "core/Simulation.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 脚本化 Rng：chance()/unit() 各自独立脚本（unit 覆盖后等级采样可控，同 chance 的 mock 模式）。
class LevelRng final : public lw::Rng {
public:
    std::vector<double> units;   // 脚本化 unit() 结果
    std::size_t nextU = 0;
    std::vector<bool> chances;   // 脚本化 chance() 结果
    std::size_t nextC = 0;
    double unit() override { return nextU < units.size() ? units[nextU++] : 0.0; }
    bool chance(double) override { return nextC < chances.size() ? chances[nextC++] : false; }
};

// 写标准 24bit BMP（54 头 + 4 字节对齐行 + BGR + 自底向上）。
void writeBmp(const std::string& path, int w, int h, const std::vector<std::array<int, 3>>& px) {
    const int rowsize = (w * 3 + 3) & ~3;
    std::vector<unsigned char> data(static_cast<std::size_t>(54 + h * rowsize), 0);
    data[0] = 'B';
    data[1] = 'M';
    const unsigned size = static_cast<unsigned>(54 + h * rowsize);
    std::memcpy(&data[2], &size, 4);
    const unsigned off = 54;
    std::memcpy(&data[10], &off, 4);
    const unsigned ih = 40;
    std::memcpy(&data[14], &ih, 4);
    std::memcpy(&data[18], &w, 4);
    std::memcpy(&data[22], &h, 4);
    const unsigned short planes = 1, bpp = 24;
    std::memcpy(&data[26], &planes, 2);
    std::memcpy(&data[28], &bpp, 2);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const auto& p = px[static_cast<std::size_t>(j * w + i)];
            std::size_t o = static_cast<std::size_t>(54 + j * rowsize + i * 3);
            data[o] = static_cast<unsigned char>(p[2]);
            data[o + 1] = static_cast<unsigned char>(p[1]);
            data[o + 2] = static_cast<unsigned char>(p[0]);
        }
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_TRUE(f != nullptr) << "cannot open " << path;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

// 全陆地图（给定尺寸）+ 默认 terrain/city 配置。
struct CityMap {
    Config cfg;
    Config::Map mcfg;
    Map map;
    explicit CityMap(int w = 20, int h = 20) : cfg(Config::loadFromJson("{}")) {
        mcfg.width = w;
        mcfg.height = h;
        map.configure(mcfg);
        map.setTerrain(cfg.terrain);
        map.setCityConfig(cfg.city);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) map.at(x, y).land = true;
    }
};

// 等级采样分支（修正幂律：P(x) ∝ n_x·(x+β)^-s，s=levelIncomeExponent+levelRankExponent=1.5，
// β=(1−min)/2=0；等级 {1,2,4,6,9}）。累计分界 u：1级<0.632、2级<0.855、4级<0.934、6级<0.977、9级其余。
TEST(City, LevelSamplingBranches) {
    const Config cfg = Config::loadFromJson("{}");
    struct Case {
        double u;
        int expect;
    };
    const Case cases[] = {{0.10, 1}, {0.60, 1}, {0.64, 2}, {0.80, 2}, {0.86, 4},
                          {0.93, 4}, {0.94, 6}, {0.97, 6}, {0.98, 9}, {0.99, 9}};
    for (const auto& c : cases) {
        LevelRng rng;
        rng.units = {c.u};
        EXPECT_EQ(Map::sampleCityLevel(cfg.city, rng), c.expect) << "u=" << c.u;
    }
    // 极端值兜底：u=0 与 u 极近 1 都在合法等级内（不越界）。
    {
        LevelRng rng;
        rng.units = {0.0};
        EXPECT_EQ(Map::sampleCityLevel(cfg.city, rng), 1);
    }
    {
        LevelRng rng;
        rng.units = {0.999999};
        EXPECT_EQ(Map::sampleCityLevel(cfg.city, rng), 9);
    }
}

// 形状推导：level → (w,h) → 基建格集合（各等级全格列举断言）+ 注册表 append-only。
TEST(City, ShapeCoverageMatchesLevel) {
    CityMap w(20, 20);
    const int levels[5] = {1, 2, 4, 6, 9};
    const std::array<std::array<int, 2>, 5> wh = {{{1, 1}, {1, 2}, {2, 2}, {2, 3}, {3, 3}}};
    int baseY = 0;
    for (int k = 0; k < 5; ++k) {
        const int cid = w.map.addCity(levels[k], 0, baseY);
        ASSERT_GE(cid, 0);
        const City& c = w.map.city(cid);
        EXPECT_EQ(c.level, levels[k]);
        EXPECT_EQ(c.w, wh[static_cast<size_t>(k)][0]) << "level " << levels[k];
        EXPECT_EQ(c.h, wh[static_cast<size_t>(k)][1]) << "level " << levels[k];
        EXPECT_EQ(c.baseX, 0);
        EXPECT_EQ(c.baseY, baseY);
        EXPECT_DOUBLE_EQ(c.area, static_cast<double>(levels[k]));
        // 基建格集合 = 锚点 + (0..w-1, 0..h-1)。
        for (int dy = 0; dy < c.h; ++dy)
            for (int dx = 0; dx < c.w; ++dx)
                EXPECT_EQ(w.map.at(dx, baseY + dy).cityId, cid) << "cell (" << dx << ","
                                                                 << baseY + dy << ")";
        baseY += 4;  // 每级换行（最大形状 3 行高 → 互不重叠）
    }
    EXPECT_EQ(w.map.totalCities(), 5);
    for (int i = 0; i < w.map.totalCities(); ++i) EXPECT_EQ(w.map.city(i).id, i);  // append-only
}

// 放置检查：合法 / 越界 / 重叠 / 锚点不可成城。
TEST(City, CanPlaceCityRules) {
    CityMap w(12, 12);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x) w.map.at(x, y).cityAllowed = true;
    EXPECT_TRUE(w.map.canPlaceCity(9, 0, 0));  // 合法 3×3
    EXPECT_TRUE(w.map.canPlaceCity(1, 11, 11));  // 单格贴右下角
    EXPECT_FALSE(w.map.canPlaceCity(9, 10, 10));  // 越界（10+3>12）
    EXPECT_FALSE(w.map.canPlaceCity(6, 11, 0));   // 越界（11+2>12，6 级形状 2×3）
    w.map.addCity(1, 0, 0);                       // 锚点 (0,0) 被占
    EXPECT_FALSE(w.map.canPlaceCity(4, 0, 0));    // 重叠
    EXPECT_FALSE(w.map.canPlaceCity(9, 0, 0));    // 重叠（形状含 (0,0)）
    w.map.at(5, 0).cityAllowed = false;           // 锚点不可成城
    EXPECT_FALSE(w.map.canPlaceCity(1, 5, 0));
    EXPECT_FALSE(w.map.canPlaceCity(2, 5, 0));    // 形状 1×2 锚点不可成城
    // 未注册等级 → 拒绝。
    EXPECT_FALSE(w.map.canPlaceCity(3, 0, 3));
    EXPECT_EQ(w.map.addCity(3, 0, 3), -1);
}

// 放置回退：采样到 9 级但形状放不下（2×2 地图）→ 回退 1 级（锚点单独可放则建 1 级城）。
TEST(City, FallbackToLevel1WhenShapeDoesNotFit) {
    const std::string path = lwtest::testArtifactPath("city_fallback.bmp");
    // 2×2 全陆；仅 (0,0) 可成城（g=255）。
    writeBmp(path, 2, 2,
             {std::array<int, 3>{32, 255, 32}, std::array<int, 3>{32, 128, 32},
              std::array<int, 3>{32, 128, 32}, std::array<int, 3>{32, 128, 32}});
    CityMap w(2, 2);
    LevelRng rng;
    rng.units = {0.99};  // 采样 9 级 → 放不下 → 回退 1 级
    rng.chances = {true, false, false, false, false, false, false, false};
    ASSERT_TRUE(w.map.loadFromBmp(path, rng));
    EXPECT_EQ(w.map.totalCities(), 1);
    const City& c = w.map.city(0);
    EXPECT_EQ(c.level, 1);  // 回退
    EXPECT_EQ(c.baseX, 0);
    EXPECT_EQ(c.baseY, 0);
    EXPECT_EQ(w.map.at(0, 0).cityId, 0);
    EXPECT_EQ(w.map.at(1, 1).cityId, -1);
}

// 锚点不可成城 + 形状放不下 → 两级都不可放 → 本格不成城。
TEST(City, NoCityWhenPlacementFailsBothLevels) {
    const std::string path = lwtest::testArtifactPath("city_none.bmp");
    // 1×1 全陆；锚点不可成城（g=128）。
    writeBmp(path, 1, 1, {std::array<int, 3>{32, 128, 32}});
    CityMap w(1, 1);
    LevelRng rng;
    rng.units = {0.99};
    rng.chances = {true, false};  // 城概率通过 → 放置两级都失败
    ASSERT_TRUE(w.map.loadFromBmp(path, rng));
    EXPECT_EQ(w.map.totalCities(), 0);
    EXPECT_EQ(w.map.at(0, 0).cityId, -1);
}

// 幂律分布（统计）——2026-08-17 调试期改均匀分布：方形成等级 {1,2,4,6,9} 各约 20%。
TEST(City, LevelDistributionApproximatesPowerLaw) {
    const std::string path = lwtest::testArtifactPath("city_powerlaw.bmp");
    std::vector<std::pair<int, int>> zones;
    for (int x = 4; x < 100; x += 4)
        for (int y = 4; y < 90; y += 4) zones.emplace_back(x, y);
    lwtest::writeTestMapBmp(path, {}, zones);
    Config cfg = lwtest::loadCfg();
    cfg.map.file = path;
    Simulation sim(cfg, 123);
    ASSERT_TRUE(sim.init());
    std::array<int, 5> count{0, 0, 0, 0, 0};  // 桶下标：等级 {1,2,4,6,9}
    for (const auto& c : sim.map().cities()) {
        const int idx = (c.level == 1) ? 0 : (c.level == 2) ? 1 : (c.level == 4) ? 2
                       : (c.level == 6) ? 3 : 4;
        count[static_cast<size_t>(idx)]++;
    }
    const int total = sim.map().totalCities();
    ASSERT_GE(total, 100) << "城概率格应产生足够城市样本";
    // 幂律分布期望（修正幂律 P∝n_x·(x+β)^-s，s=1.5，β=0；等级 {1,2,4,6,9} 归一化权重）：
    // {0.631, 0.223, 0.079, 0.043, 0.023}（容差断言"大致符合幂律"；优先级递减）。
    const double expect[5] = {0.631, 0.223, 0.079, 0.043, 0.023};
    for (int i = 0; i < 5; ++i) {
        const double observed = static_cast<double>(count[static_cast<size_t>(i)]) / total;
        EXPECT_NEAR(observed, expect[static_cast<size_t>(i)], 0.12) << "level bucket " << i;
    }
    // 至少存在一例 >1 级城市（均匀分布非退化到全是 1 级）。
    int high = 0;
    for (int i = 1; i < 5; ++i) high += count[static_cast<size_t>(i)];
    EXPECT_GT(high, 0);
}

// 确定性：同 mapSeed 两次 init → 逐格 cityId + 城市注册表一致。
TEST(City, SameMapSeedDeterministic) {
    Config cfg = lwtest::loadCfg();
    Simulation a(cfg, 42), b(cfg, 42);
    ASSERT_TRUE(a.init());
    ASSERT_TRUE(b.init());
    for (int y = 0; y < a.map().height(); ++y)
        for (int x = 0; x < a.map().width(); ++x)
            EXPECT_EQ(a.map().at(x, y).cityId, b.map().at(x, y).cityId)
                << "(" << x << "," << y << ")";
    ASSERT_EQ(a.map().cities().size(), b.map().cities().size());
    for (std::size_t i = 0; i < a.map().cities().size(); ++i) {
        const auto& ca = a.map().cities()[i];
        const auto& cb = b.map().cities()[i];
        EXPECT_EQ(ca.level, cb.level) << "city " << i;
        EXPECT_EQ(ca.baseX, cb.baseX) << "city " << i;
        EXPECT_EQ(ca.baseY, cb.baseY) << "city " << i;
        EXPECT_EQ(ca.w, cb.w) << "city " << i;
        EXPECT_EQ(ca.h, cb.h) << "city " << i;
        EXPECT_EQ(ca.ownerId, cb.ownerId) << "city " << i;
    }
}

}  // namespace
