// test_mountain.cpp — P5 山地/地形基图单测（解析/邻海修正/编码规则/移动/产兵）。
// 覆盖开发计划 P5：海陆确定性规则、R→山概率、G→城概率、B 闲置、山城共存、邻海修正、
// 基线图零山、陆↔山概率进/反弹、进山减速、出山复原、山→海/海→山。
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <entt/entt.hpp>

#include "core/Config.h"
#include "core/Random.h"
#include "core/Simulation.h"
#include "sim/SpatialHash.h"
#include "sim/components.h"
#include "sim/systems/MovementSystem.h"
#include "sim/systems/SpawnSystem.h"
#include "world/Map.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// ---- 地图解析 / 邻海修正 ----

Map loadMap(const std::string& file) {
    Map map;
    const Config cfg = lwtest::loadCfg();
    map.configure(cfg.map);
    map.setTerrain(cfg.terrain);
    Rng rng(42);
    EXPECT_TRUE(map.loadFromBmp(file, rng));
    return map;
}

TEST(Mountain, BaselineMapHasZeroMountains) {
    // 防基线漂移：基线图 map_bigIslands（转换后）无山（无 r≥128 的山标记）。
    const Map map = loadMap("data/map_bigIslands.bmp");
    int mountains = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.at(x, y).mountain) ++mountains;
    EXPECT_EQ(mountains, 0);
}

TEST(Mountain, DemoMapFlagsExactlyPaintedCells) {
    // 自包含基图：3 个 r=255 山标记（确定性山）+ 一片海。验证 r=255 → 恰成这些格的山。
    const std::string path = lwtest::testArtifactPath("mtn_demo.bmp");
    lwtest::writeTestMapBmp(path, {{50, 50}, {30, 80}, {80, 30}}, {},
                            {{0, 0}, {0, 1}, {1, 0}});  // 角落海
    lw::Config cfg = lwtest::loadCfg();
    cfg.map.file = path;
    lw::Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    const auto& map = sim.map();
    int mtn = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.at(x, y).mountain) ++mtn;
    EXPECT_EQ(mtn, 3);
    EXPECT_TRUE(map.at(50, 50).mountain);
    EXPECT_TRUE(map.at(30, 80).mountain);
    EXPECT_TRUE(map.at(80, 30).mountain);
    EXPECT_FALSE(map.at(0, 0).mountain);  // 海
    EXPECT_FALSE(map.at(60, 60).mountain);  // 普通陆
}

TEST(Mountain, CoastCorrectionClearsSeaAdjacent) {
    // 手工建 3×3：中心是山，但右上角是海 → 8 邻域含海 → 中心清为普通陆。
    Config cfg = lwtest::loadCfg();
    cfg.map.width = 3;
    cfg.map.height = 3;
    Map map;
    map.configure(cfg.map);
    map.setTerrain(cfg.terrain);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) map.at(x, y).land = true;
    map.at(1, 2).land = false;         // 右上角海（邻居中心 (1,1)）
    map.at(1, 1).mountain = true;      // 邻海 → 待清
    map.at(2, 0).mountain = true;      // 内陆山 → 保留
    map.correctMountainCoast();
    EXPECT_FALSE(map.at(1, 1).mountain);  // 邻海清除
    EXPECT_TRUE(map.at(2, 0).mountain);   // 内陆保留
}

TEST(Mountain, TiledCoastCorrectionClearsPointAdjacentMountain) {
    Config cfg = lwtest::loadCfg();
    cfg.map.tiling = "tri";
    cfg.map.width = 12;
    cfg.map.height = 12;
    Map map;
    map.configure(cfg.map);
    map.setTerrain(cfg.terrain);
    for (int idx = 0; idx < map.cellCount(); ++idx) map.atIndex(idx).land = true;

    const int mountain = 2 * (3 * map.geom().cols + 3);  // interior upward triangle
    int sea = -1;
    for (int k = 0; k < map.geom().pointNeighborCount(mountain); ++k) {
        const int candidate = map.geom().pointNeighbor(mountain, k);
        bool edgeNeighbor = false;
        for (int ek = 0; ek < map.geom().neighborCount(mountain); ++ek)
            if (map.geom().neighbor(mountain, ek) == candidate) edgeNeighbor = true;
        if (candidate >= 0 && !edgeNeighbor) {
            sea = candidate;
            break;
        }
    }
    ASSERT_GE(sea, 0);
    map.atIndex(mountain).mountain = true;
    map.atIndex(sea).land = false;
    map.correctMountainCoast();
    EXPECT_FALSE(map.atIndex(mountain).mountain);
}

// ---- 编码规则（手写小 BMP 直接验证 loadFromBmp）----

// 写标准 24bit BMP：54 头 + 每行 4 字节对齐 + BGR + 自底向上（j 行 = 图像底部行）。
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

struct TinyMap {
    Config cfg;
    Map map;
    lwtest::MockRng rng;  // chance() 返回预设结果（忽略 p）→ 可逐次控制两骰

    explicit TinyMap(int w, int h) : cfg(lwtest::loadCfg()) {
        cfg.map.width = w;
        cfg.map.height = h;
        map.configure(cfg.map);
        map.setTerrain(cfg.terrain);
    }
    void write(const std::vector<std::array<int, 3>>& px) {
        writeBmp(lwtest::testArtifactPath("tiny_map.bmp"), cfg.map.width, cfg.map.height, px);
    }
    void load() {
        ASSERT_TRUE(map.loadFromBmp(lwtest::testArtifactPath("tiny_map.bmp"), rng));
    }
};

TEST(MountainEnc, SeaWhenAnyChannelBelow32) {
    // 任一分量 <32 → 海（确定性）；全分量 ≥32 → 陆。
    TinyMap t(3, 1);
    t.rng.results = {false, false};  // 仅陆格 (1,0) 消耗两骰
    t.write({std::array<int, 3>{0, 0, 0}, std::array<int, 3>{31, 200, 200},
             std::array<int, 3>{255, 0, 255}});
    t.load();
    EXPECT_FALSE(t.map.at(0, 0).land);
    EXPECT_FALSE(t.map.at(1, 0).land);  // min=31 <32
    EXPECT_FALSE(t.map.at(2, 0).land);  // min=0 <32
    EXPECT_TRUE(t.map.at(0, 0).cityId == -1);
}

TEST(MountainEnc, MountainProbabilityFromRChannel) {
    TinyMap t(1, 1);
    t.rng.results = {true};  // 山骰=true（当前每陆格仅一骰：山；城许可为确定性 ramp(G)>0）
    t.write({std::array<int, 3>{255, 32, 32}});  // r=255 → p_mtn=1；g=32 → 城许可 false
    t.load();
    EXPECT_TRUE(t.map.at(0, 0).land);
    EXPECT_TRUE(t.map.at(0, 0).mountain);
    EXPECT_FALSE(t.map.at(0, 0).cityId >= 0);
}

TEST(MountainEnc, CityProbabilityFromGChannel) {
    TinyMap t(1, 1);
    t.rng.results = {false};  // 山骰=false；城许可由 ramp(G) 决定（确定性，非骰）
    t.write({std::array<int, 3>{32, 255, 32}});  // g=255 → 城许可 true；r=32 → p_mtn=0
    t.load();
    EXPECT_TRUE(t.map.at(0, 0).land);
    EXPECT_TRUE(t.map.at(0, 0).cityId >= 0);
    EXPECT_FALSE(t.map.at(0, 0).mountain);
}

TEST(MountainEnc, MountainAndCityCoexist) {
    TinyMap t(1, 1);
    t.rng.results = {true, true};  // 两骰独立 → 山+城同格
    t.write({std::array<int, 3>{255, 255, 32}});
    t.load();
    EXPECT_TRUE(t.map.at(0, 0).mountain);
    EXPECT_TRUE(t.map.at(0, 0).cityId >= 0);
}

TEST(MountainEnc, BChannelIdleButPartOfSeaRule) {
    // B 闲置（不产生山/城），但 b<32 仍判海。
    TinyMap t(2, 1);
    t.rng.results = {false, false};  // (0,0) 陆格：城/山均 false
    t.write({std::array<int, 3>{32, 32, 255}, std::array<int, 3>{32, 32, 31}});
    t.load();
    EXPECT_TRUE(t.map.at(0, 0).land);
    EXPECT_FALSE(t.map.at(0, 0).mountain);
    EXPECT_FALSE(t.map.at(0, 0).cityId >= 0);
    EXPECT_FALSE(t.map.at(1, 0).land);  // b=31 <32 → 海
}

TEST(MountainEnc, FractionalRampConsumesRng) {
    // r=200 → p_mtn=(200-128)/127≈0.567：验证山骰从预设结果取（前一个为城市骰的旧写法已废）。
    TinyMap t(1, 1);
    t.rng.results = {true};   // 山骰=true → 中等概率抽中
    t.write({std::array<int, 3>{200, 32, 32}});
    t.load();
    EXPECT_TRUE(t.map.at(0, 0).mountain);
    EXPECT_FALSE(t.map.at(0, 0).cityId >= 0);
    // 另一个中等概率没抽中：
    t.rng.results = {false, false};
    t.load();
    EXPECT_FALSE(t.map.at(0, 0).mountain);
}

TEST(MountainEnc, CoastCorrectionAppliedDuringLoad) {
    // 3×3：山标记 (0,0) 邻海(1,0) → 邻海修正清掉；山标记 (2,2) 全邻陆 → 保留。
    const std::array<int, 3> P{32, 32, 32}, M{255, 32, 32}, S{0, 0, 0};
    TinyMap t(3, 3);
    // 8 个陆格各消耗一骰（山；城为确定性许可）。全部置 山=true，使两处 M 格都成山，
    // 再经邻海修正选择性清除——与洗牌顺序无关，稳健。
    t.rng.results = {true, true, true, true, true, true, true, true};
    t.write({M, S, P, P, P, P, P, P, M});
    t.load();
    EXPECT_FALSE(t.map.at(0, 0).mountain);  // 邻海 → 清
    EXPECT_TRUE(t.map.at(0, 0).land);       // 仍陆地
    EXPECT_TRUE(t.map.at(2, 2).mountain);   // 内陆 → 保留
}

// ---- 移动（复刻 test_army.cpp TestWorld 模式，含 inMountain）----

struct MountainWorld {
    Config cfg;
    Map map;
    lwtest::MockRng rng;
    std::vector<Faction> factions;
    std::vector<PendingSpawn> pending;
    std::vector<DeathEvent> deaths;
    entt::registry reg;
    SpatialHash hash;
    double goSeaProb = 1.0;
    int ttime = 0;

    explicit MountainWorld(int width = 5, int height = 5) : cfg(lwtest::loadCfg()) {
        cfg.map.width = width;
        cfg.map.height = height;
        map.configure(cfg.map);
        map.setTerrain(cfg.terrain);
        factions.resize(static_cast<size_t>(kFactionTotal));
        for (int id = 0; id < kFactionTotal; ++id)
            factions[static_cast<size_t>(id)].initFromDef(cfg.factions[static_cast<size_t>(id)], cfg);
    }
};

MoveContext makeCtx(MountainWorld& w) {
    return MoveContext{w.map,     w.factions, w.rng,   w.pending, w.deaths,
                       w.reg,     w.hash,     w.goSeaProb, w.ttime, w.cfg};
}

entt::entity addArmy(MountainWorld& w, double x, double y, int fid, ArmyType type,
                     double angle, double speed, bool onland, bool inMountain = false) {
    auto e = w.reg.create();
    w.reg.emplace<comp::Position>(e, x, y);
    w.reg.emplace<comp::Velocity>(e, angle);
    w.reg.emplace<comp::Speed>(e, speed);
    w.reg.emplace<comp::OnLand>(e, onland);
    w.reg.emplace<comp::MountainState>(e, inMountain);
    w.reg.emplace<comp::FactionId>(e, fid);
    w.reg.emplace<comp::UnitType>(e, type);
    w.reg.emplace<comp::Collider>(e, 1.1);
    w.reg.emplace<comp::LandHistory>(e, 0);
    return e;
}

void moveOnce(MountainWorld& w, entt::entity e) {
    auto ctx = makeCtx(w);
    MovementSystem::moveArmy(ctx, e);
}

TEST(MountainMove, EnterMountainSlowsAndKeepsState) {
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;   // 己方平地
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 1;   // 己方山（避免征服分支干扰）
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {true};  // 进山成功
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.15);  // ×0.5
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 2.1, 1e-9);  // 剩余步长减半后继续入山
}

TEST(MountainMove, EnterMountainFailsBounces) {
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {false};  // 进山失败 → 水平反弹，不攻占
    moveOnce(w, e);
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);  // 未减速
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹 + 随机小偏置
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);  // 未占领目标山格（仍己方，未触发征服）
}

TEST(MountainMove, LeaveMountainRestoresSpeed) {
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).belongi = 1;  // 平地目标
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true, /*inMountain=*/true);
    moveOnce(w, e);  // 离开山地 → 速度复原
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);  // /0.5
}

TEST(MountainMove, SeaToMountainLandingEntersMountain) {
    MountainWorld w;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, false);  // 海上
    w.rng.results = {true};  // 山地骰通过 → 登陆进山
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);   // 登陆到山 → 进山
    EXPECT_NEAR(w.reg.get<comp::Speed>(e).value, 0.15, 1e-9);    // 登陆/0.5 再进山×0.5 = 海速
}

TEST(MountainMove, SeaToMountainLandingBounces) {
    // 细节改进：海→山（不管当前格）也掷山地阻挡；失败 → 反弹留海、不登陆。
    MountainWorld w;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, false);  // 海上
    w.rng.results = {false};  // 山地阻挡 → 反弹
    moveOnce(w, e);
    EXPECT_FALSE(w.reg.get<comp::OnLand>(e).value);   // 未登陆
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹 + 随机小偏置
}

TEST(MountainMove, MountainToMountainBounces) {
    // 细节改进：山→山（不管当前格）也掷山地阻挡；失败 → 反弹（留在原地山地）。
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).mountain = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true, /*inMountain=*/true);
    w.rng.results = {false};  // 山地阻挡 → 反弹
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);   // 仍留在山地
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 反弹
}

TEST(MountainMove, MountainToSeaFailedLandingKeepsMountainState) {
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).mountain = true;
    w.map.at(1, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true,
                     /*inMountain=*/true);
    w.rng.results = {false};  // 下海失败 → 反弹回原山地格
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.15);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
}

TEST(MountainMove, MountainToEnemyPlainBounceKeepsMountainState) {
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).mountain = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true,
                     /*inMountain=*/true);
    w.rng.results = {true};  // 敌方领土反弹
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.15);
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);  // 征服副作用仍保留
}

TEST(MountainMove, PlainToEnemyMountainBounceKeepsPlainState) {
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    w.rng.results = {true, true};  // 进山通过，敌方反弹
    moveOnce(w, e);
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);  // 征服副作用仍保留
}

TEST(MountainMove, MountainToMountainPasses) {
    // 山→山 山地骰通过 → 继续前进，状态不变（已在山地不重复减速）。
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).mountain = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true, /*inMountain=*/true);
    w.rng.results = {true};  // 山地骰通过
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 1e-9);
    EXPECT_GT(w.reg.get<comp::Position>(e).x, 2.0);  // 越过边界进入新山格
}

TEST(MountainMove, PioneerMountainNoSlow) {
    // 细节改进：开拓兵在山地不减速（进山 inMountain=true 但速度不变）。
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::pioneer, 0.0, 0.3, true);
    w.rng.results = {true};  // 进山成功
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);  // 开拓不减速
}

TEST(MountainMove, PioneerMountainEnterChanceHigher) {
    // 当前 data/units.jsonc 将开拓兵山地进入倍率设为 2，最终按 1.0 封顶。
    MountainWorld w;
    EXPECT_DOUBLE_EQ(lw::mountainEnterChanceFor(w.cfg, static_cast<int>(ArmyType::normal)), 0.5);
    EXPECT_DOUBLE_EQ(lw::mountainEnterChanceFor(w.cfg, static_cast<int>(ArmyType::vanguard)), 0.5);
    EXPECT_DOUBLE_EQ(lw::mountainEnterChanceFor(w.cfg, static_cast<int>(ArmyType::pioneer)), 1.0);
}

TEST(MountainMove, VanguardCrossesEnemyMountainBothPass) {
    // 敌方+山地：先锋两骰（山地 + 敌方反弹）独立且都通过 → 不反弹、进入并征服。
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::vanguard, 0.0, 0.3, true);
    w.rng.results = {true, false};  // 山地骰过 + 敌方反弹不中 → 进入
    moveOnce(w, e);
    EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_EQ(w.map.at(2, 1).belongi, 1);   // 征服目标格
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.0, 1e-9);  // 不反弹
}

TEST(MountainMove, NormalAlwaysBouncesOnEnemyMountain) {
    // 敌方+山地：非先锋（bounceMult=1.0）敌方阻挡必然反弹——用真实 Rng 验证（chance(1.0) 恒真）。
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    w.map.at(2, 1).land = true;
    w.map.at(2, 1).mountain = true;
    w.map.at(2, 1).belongi = 2;
    w.factions[2].landCount = 1;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3, true);
    lw::Rng realRng(1234);  // 绕开 MockRng：真实 Rng 的 chance(1.0) 恒 true
    MoveContext ctx{w.map, w.factions, realRng, w.pending, w.deaths,
                    w.reg, w.hash, w.goSeaProb, w.ttime, w.cfg};
    MovementSystem::moveArmy(ctx, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, kPi, 0.02);  // 必反弹
}

TEST(MountainMove, MountainToSeaRestoresBeforeGoingSea) {
    MountainWorld w;
    w.map.at(1, 1).land = true;
    w.map.at(1, 1).belongi = 1;
    // (2,1) 默认海
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.15, true, /*inMountain=*/true);
    w.rng.results = {true};  // 下海成功
    moveOnce(w, e);
    EXPECT_FALSE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);  // 出山
    EXPECT_NEAR(w.reg.get<comp::Speed>(e).value, 0.15, 1e-9);    // 复原0.3 再 /0.5 = 0.15
}

TEST(MountainMove, SpawnInMountainScalesSpeed) {
    // 产兵在山格 → inMountain=true 且速度按山地系数缩放（与 moveArmy 语义自洽）。
    // 自包含基图：1 个 r=255 山标记。
    const std::string path = lwtest::testArtifactPath("spawn_mtn.bmp");
    lwtest::writeTestMapBmp(path, {{52, 47}}, {});
    Config cfg = lwtest::loadCfg();
    cfg.map.file = path;
    Simulation sim(cfg, 7);
    ASSERT_TRUE(sim.init());
    const int mx = 52, my = 47;
    EXPECT_TRUE(sim.map().at(mx, my).mountain);
    auto e = SpawnSystem::spawnArmy(sim, mx + 0.5, my + 0.5, 1, ArmyType::normal);
    ASSERT_TRUE(e != entt::null);
    EXPECT_TRUE(sim.registry().get<comp::MountainState>(e).inMountain);
    EXPECT_NEAR(sim.registry().get<comp::Speed>(e).value,
                cfg.army.baseSpeed * cfg.terrain.mountainSpeedMult, 1e-12);
}

TEST(MountainMove, PioneerSpawnInMountainNotSlowed) {
    // 细节改进：开拓兵出生在山格 → inMountain=true 但速度不缩放（山地不减速）。
    const std::string path = lwtest::testArtifactPath("spawn_pioneer_mtn.bmp");
    lwtest::writeTestMapBmp(path, {{52, 47}}, {});
    Config cfg = lwtest::loadCfg();
    cfg.map.file = path;
    Simulation sim(cfg, 7);
    ASSERT_TRUE(sim.init());
    auto e = SpawnSystem::spawnArmy(sim, 52 + 0.5, 47 + 0.5, 1, ArmyType::pioneer);
    ASSERT_TRUE(e != entt::null);
    EXPECT_TRUE(sim.registry().get<comp::MountainState>(e).inMountain);
    EXPECT_DOUBLE_EQ(sim.registry().get<comp::Speed>(e).value, cfg.army.baseSpeed);  // 未减速
}

}  // namespace
