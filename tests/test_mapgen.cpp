// test_mapgen.cpp — 随机地图生成（开发计划 P6）。
// 确定性：同 seed+params → 逐字节一致；不同 seed → 不同图。
// 可加载性：生成 BMP 走现有 Map::loadFromBmp → 海陆比/山/城分布符合参数（骰子期望值）。
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "world/Map.h"
#include "world/MapGenerator.h"
#include "world/Bmp24.h"
#include "world/TerrainCodec.h"
#include "TestUtil.h"

namespace {

// 读整文件为字节（比较确定性）。
std::vector<char> readFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

// 与 Map::loadFromBmp 一致的 ramp：p = clamp((x-128)/127, 0, 1)。
double ramp(int x) {
    return lw::terrain::ramp(x, lw::Config::Terrain{});
}

// 从生成 BMP 反解每格 R/G 概率（文件行 j = 世界行 j，行按 4 字节对齐）。
// 返回三通道值（r,g,b），供计算期望山/城数。
std::vector<std::array<int, 3>> decodeBmp(const std::string& path, int w, int h) {
    std::vector<char> raw = readFile(path);
    const int rowsize = (w * 3 + 3) & ~3;
    std::vector<std::array<int, 3>> px(static_cast<size_t>(w) * h);
    for (int j = 0; j < h; ++j) {
        const size_t rowStart = 54 + static_cast<size_t>(j) * rowsize;
        for (int i = 0; i < w; ++i) {
            px[static_cast<size_t>(j) * w + i] = {
                static_cast<unsigned char>(raw[rowStart + static_cast<size_t>(i) * 3 + 2]),
                static_cast<unsigned char>(raw[rowStart + static_cast<size_t>(i) * 3 + 1]),
                static_cast<unsigned char>(raw[rowStart + static_cast<size_t>(i) * 3 + 0])};
        }
    }
    return px;
}

// 生成 → 用参数尺寸的 Map 加载，返回已加载图。
lw::Map loadGenerated(std::uint32_t seed, const lw::MapGenParams& p, const std::string& path) {
    EXPECT_TRUE(lw::MapGenerator::generate(path, seed, p));
    lw::Config cfg = lwtest::loadCfg();
    cfg.map.width = p.width;
    cfg.map.height = p.height;
    cfg.map.file = path;
    lw::Map map;
    map.configure(cfg.map);
    map.setTerrain(cfg.terrain);
    lw::Rng rng(seed);  // 地图骰子用同一地图种子（与正式流程 mapRng_ 一致）
    EXPECT_TRUE(map.loadFromBmp(path, rng));
    return map;
}

TEST(MapGen, DeterministicByteIdentical) {
    const lw::MapGenParams p{105, 95, 0.40, 0.08, 0.02};
    ASSERT_TRUE(lw::MapGenerator::generate("build/_gen_a.bmp", 42, p));
    ASSERT_TRUE(lw::MapGenerator::generate("build/_gen_b.bmp", 42, p));
    EXPECT_EQ(readFile("build/_gen_a.bmp"), readFile("build/_gen_b.bmp"));
}

TEST(MapGen, DifferentSeedDifferentMap) {
    const lw::MapGenParams p{105, 95, 0.40, 0.08, 0.02};
    ASSERT_TRUE(lw::MapGenerator::generate("build/_gen_s1.bmp", 1, p));
    ASSERT_TRUE(lw::MapGenerator::generate("build/_gen_s2.bmp", 2, p));
    EXPECT_NE(readFile("build/_gen_s1.bmp"), readFile("build/_gen_s2.bmp"));
}

TEST(MapGen, CachePathIncludesAllGenerationParameters) {
    lw::MapGenParams a{105, 95, 0.20, 0.08, 0.02, 0.3, false};
    lw::MapGenParams b = a;
    b.forceCoast = true;
    EXPECT_NE(lw::MapGenerator::defaultPath(42, a), lw::MapGenerator::defaultPath(42, b));

    b = a;
    b.cityDensity = 0.021;
    EXPECT_NE(lw::MapGenerator::defaultPath(42, a), lw::MapGenerator::defaultPath(42, b));
}

TEST(Bmp24, RoundTripUsesStandardStrideAndDimensions) {
    const std::string path = "build/_bmp24_stride.bmp";
    const std::vector<std::array<unsigned char, 3>> pixels = {
        {1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}, {13, 14, 15}, {16, 17, 18}};
    std::string err;
    ASSERT_TRUE(lw::writeBmp24(path, 2, 3, pixels, &err)) << err;
    lw::Bmp24Image image;
    ASSERT_TRUE(lw::readBmp24(path, image, &err)) << err;
    EXPECT_EQ(image.width, 2);
    EXPECT_EQ(image.height, 3);
    EXPECT_EQ(image.pixels, pixels);
}

// ---- 强制边缘为海（2026-08-06 用户要求）----

// 统计海格数与最外一圈（d==0）是否全海。
struct CoastStats {
    int sea = 0;
    int ring = 0;      // 最外一圈格数
    int ringSea = 0;   // 最外一圈中海格数
};
CoastStats coastStats(const lw::Map& map) {
    CoastStats s;
    const int w = map.width(), h = map.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!map.at(x, y).land) ++s.sea;
            const int d = std::min({x, w - 1 - x, y, h - 1 - y});
            if (d == 0) {
                ++s.ring;
                if (!map.at(x, y).land) ++s.ringSea;
            }
        }
    }
    return s;
}

TEST(MapGen, ForceCoastAtZeroSeaIsExactlyRing) {
    // 陆地占比 100%（seaRatio=0）+ 强制边缘为海 → 只有最外一圈为海。
    const lw::MapGenParams p{48, 40, 0.0, 0.05, 0.02, 0.3, true};
    const lw::Map map = loadGenerated(42, p, "build/_gen_coast0.bmp");
    const CoastStats s = coastStats(map);
    EXPECT_EQ(s.ringSea, s.ring) << "最外一圈必须全海";
    EXPECT_EQ(s.sea, s.ring) << "陆地占比 100% 时只能有一圈海（实际海 " << s.sea
                              << " vs 圈 " << s.ring << "）";
    // 内圈（d>=1）必须全是陆地。
    const int w = map.width(), h = map.height();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int d = std::min({x, w - 1 - x, y, h - 1 - y});
            if (d >= 1) {
                EXPECT_TRUE(map.at(x, y).land) << "内圈 (" << x << "," << y << ") 应为陆";
            }
        }
}

TEST(MapGen, ForceCoastWithoutZeroKeepsRingAndRatio) {
    // 有海占比 + 强制边缘为海 → 最外一圈必海、海陆比仍符合参数。
    const lw::MapGenParams p{105, 95, 0.30, 0.08, 0.04, 0.3, true};
    const lw::Map map = loadGenerated(42, p, "build/_gen_coast30.bmp");
    const CoastStats s = coastStats(map);
    EXPECT_EQ(s.ringSea, s.ring) << "最外一圈必须全海";
    const double ratio = static_cast<double>(s.sea) / (map.width() * map.height());
    EXPECT_NEAR(ratio, p.seaRatio, 0.05);
}

TEST(MapGen, NoForceCoastAtZeroSeaIsAllLand) {
    // 不勾强制边缘为海、seaRatio=0 → 全陆地。
    const lw::MapGenParams p{40, 40, 0.0, 0.05, 0.02, 0.3, false};
    const lw::Map map = loadGenerated(7, p, "build/_gen_allland.bmp");
    const CoastStats s = coastStats(map);
    EXPECT_EQ(s.sea, 0);
}

TEST(MapGen, LoadableAndParamsHeld) {
    const lw::MapGenParams p{105, 95, 0.45, 0.10, 0.05};
    const std::string path = "build/_gen_load.bmp";
    const lw::Map map = loadGenerated(42, p, path);
    const auto px = decodeBmp(path, p.width, p.height);

    const int w = map.width(), h = map.height();
    int sea = 0, mtn = 0, city = 0;
    int mtnWithNeighbor = 0;
    double expectedMtn = 0.0, expectedCity = 0.0;
    double coastCityP = 0.0, interiorCityP = 0.0;
    int coastLand = 0, interiorLand = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& c = map.at(x, y);
            const auto& rgb = px[static_cast<size_t>(y) * w + x];
            if (!c.land) {
                ++sea;
                EXPECT_EQ(rgb[0], 0) << "海格应纯黑 (" << x << "," << y << ")";
                continue;
            }
            expectedMtn += ramp(rgb[0]);  // R → 山概率
            expectedCity += ramp(rgb[1]);  // G → 城概率
            if (c.mountain) {
                ++mtn;
                bool nb = false;  // 山聚集（地形作用：成片山脉而非零星散布）
                for (int dy = -1; dy <= 1 && !nb; ++dy)
                    for (int dx = -1; dx <= 1 && !nb; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                        if (map.at(nx, ny).mountain) nb = true;
                    }
                if (nb) ++mtnWithNeighbor;
            }
            // P13：只数城市锚点（baseX,baseY）→ 城市实体数（每格基建不重复计数）。
            if (c.cityId >= 0 && map.city(c.cityId).baseX == x && map.city(c.cityId).baseY == y)
                ++city;
            // 城概率（G 通道 ramp）的地形偏好：沿海 > 内陆 > 山顶。
            const double cp = ramp(rgb[1]);
            bool nearSea = false;
            for (int dy = -1; dy <= 1 && !nearSea; ++dy)
                for (int dx = -1; dx <= 1 && !nearSea; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    if (!map.at(nx, ny).land) nearSea = true;
                }
            if (c.mountain) {
                // 山顶：不计入沿海/内陆统计（单独在下方断言期望很少）
            } else if (nearSea) {
                coastCityP += cp;
                ++coastLand;
            } else {
                interiorCityP += cp;
                ++interiorLand;
            }
        }
    }

    // 海陆比落在容差内。
    EXPECT_NEAR(static_cast<double>(sea) / (w * h), p.seaRatio, 0.06);

    // 山数 ≈ 期望（骰子期望 = Σ ramp(R)），±50% 容差（0.10×内陆 ≈ 数百，波动远小于半幅）。
    EXPECT_GE(mtn, static_cast<int>(expectedMtn * 0.5));
    EXPECT_LE(mtn, static_cast<int>(expectedMtn * 1.5));
    // 山只在内陆（loadFromBmp 已做邻海修正；生成器仅内陆格赋山概率 → 期望 0 邻海）。
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!map.at(x, y).mountain) continue;
            bool nearSea = false;
            for (int dy = -1; dy <= 1 && !nearSea; ++dy)
                for (int dx = -1; dx <= 1 && !nearSea; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    if (!map.at(nx, ny).land) nearSea = true;
                }
            EXPECT_FALSE(nearSea) << "山邻海 (" << x << "," << y << ")";
        }
    }
    // 城数 ≈ 期望（Σ ramp(G) = cityDensity×陆格数），±50% 容差。
    EXPECT_GE(city, static_cast<int>(expectedCity * 0.5));
    EXPECT_LE(city, static_cast<int>(expectedCity * 1.5));
    // 地形作用（2026-08-06 用户反馈"分布太均匀"）：山成片（≥70% 有山邻居）、
    // 城概率沿海 > 内陆（避免山顶）。
    EXPECT_GE(mtnWithNeighbor, static_cast<int>(mtn * 0.7)) << "山脉应聚集而非零星散布";
    if (coastLand > 0 && interiorLand > 0) {
        EXPECT_GT(coastCityP / coastLand, interiorCityP / interiorLand)
            << "城概率应沿海高于内陆";
    }
}

// ---- P12：六/三角 forceCoast（外圈恒海，多边形顶点贴边判定，2026-08 修复）----

// 生成 lwmap → Map 加载（六/三角）。
lw::Map loadGeneratedTiled(std::uint32_t seed, const lw::MapGenParams& p, const std::string& path) {
    EXPECT_TRUE(lw::MapGenerator::generate(path, seed, p));
    lw::Config cfg = lwtest::loadCfg();
    cfg.map.width = p.width;
    cfg.map.height = p.height;
    cfg.map.tiling = lw::tilingName(p.tiling);
    cfg.map.file = path;
    lw::Map map;
    map.configure(cfg.map);
    map.setTerrain(cfg.terrain);
    lw::Rng rng(seed);  // 地图骰子用同一地图种子
    EXPECT_TRUE(map.loadFromLwmap(path, rng));
    return map;
}

// 规范：勾选"强制边缘为海"且陆地比例 100%（seaRatio=0）时，只有真实拥有界外点邻居的格是海。
// 不再按密铺块的行/列整圈处理；48×40 与 120×120（用户 GUI 尺寸）两组。
TEST(MapGen, TiledForceCoastUsesActualPointBoundary) {
    const lw::TilingType tilings[2] = {lw::TilingType::Hex, lw::TilingType::Tri};
    for (const lw::TilingType t : tilings) {
        for (const int sz : {48, 120}) {
            const lw::MapGenParams p{sz, sz, 0.0, 0.05, 0.02, 0.3, true, t};
            const std::string path = std::string("build/_gen_fc_") +
                                     (t == lw::TilingType::Hex ? "hex" : "tri") + "_" +
                                     std::to_string(sz) + ".lwmap";
            const lw::Map map = loadGeneratedTiled(42, p, path);
            const lw::TilingGeom& g = map.geom();
            for (int r = 0; r < g.rows; ++r) {
                for (int c = 0; c < g.cols; ++c) {
                    const int oCount = (t == lw::TilingType::Tri) ? 2 : 1;
                    for (int o = 0; o < oCount; ++o) {
                        const int idx = (t == lw::TilingType::Tri) ? 2 * (r * g.cols + c) + o
                                                                   : r * g.cols + c;
                        bool outer = false;
                        for (int k = 0; k < g.pointNeighborCount(idx); ++k)
                            if (g.pointNeighbor(idx, k) < 0) outer = true;
                        const bool sea = !map.atIndex(idx).land;
                        EXPECT_EQ(sea, outer)
                            << "tiling=" << static_cast<int>(t) << " sz=" << sz
                            << " r=" << r << " c=" << c << " o=" << o
                            << " (有界外点邻居的格应海、其余应陆)";
                    }
                }
            }
        }
    }
}

TEST(MapGen, TableTilingForceCoastUsesActualPointBoundary) {
    const lw::MapGenParams p{48, 48, 0.0, 0.05, 0.02, 0.3, true,
                             lw::TilingType::Arch31212};
    const lw::Map map = loadGeneratedTiled(42, p, "build/_gen_fc_arch31212.lwmap");
    const lw::TilingGeom& g = map.geom();
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        bool boundary = false;
        for (int k = 0; k < g.pointNeighborCount(idx); ++k)
            if (g.pointNeighbor(idx, k) < 0) boundary = true;
        EXPECT_EQ(!map.atIndex(idx).land, boundary) << "idx=" << idx;
    }
}

TEST(MapGen, MixedAreaTilingGradientDoesNotFavorLargePolygons) {
    // arch_31212 同时包含三角形和正十二边形；全陆地场隔离边缘/海影响，
    // 检查局部坡度排序不会因为多边形面积和邻居数量不同而偏向十二边形。
    const lw::MapGenParams p{96, 96, 0.0, 0.20, 0.0, 0.3, false,
                             lw::TilingType::Arch31212};
    const lw::Map map = loadGeneratedTiled(42, p, "build/_gen_arch31212_gradient.lwmap");
    int triangles = 0, dodecagons = 0, triangleMountains = 0, dodecagonMountains = 0;
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        const int sides = map.geom().neighborCount(idx);
        if (sides == 3) {
            ++triangles;
            if (map.atIndex(idx).mountain) ++triangleMountains;
        } else if (sides == 12) {
            ++dodecagons;
            if (map.atIndex(idx).mountain) ++dodecagonMountains;
        }
    }
    ASSERT_GT(triangles, 0);
    ASSERT_GT(dodecagons, 0);
    const double triangleRate = static_cast<double>(triangleMountains) / triangles;
    const double dodecagonRate = static_cast<double>(dodecagonMountains) / dodecagons;
    EXPECT_LT(std::fabs(triangleRate - dodecagonRate), 0.20)
        << "triangle rate=" << triangleRate << " dodecagon rate=" << dodecagonRate;
}

}  // namespace
