// test_river.cpp — 河流玩法单测（《河流系统开发文档》§6：河骰 R3/R4）。
// 判定顺序权威：河 → 山 → 敌。河骰失败 → 反弹且**不进入/不占领**；通过 → 不减速。
// 方形路径走同一条 processEnteredCell，但边序号由 boundaryCode 映射；密铺路径直接用 crossEdge
// 返回的边序号。两条路径各覆盖一次。
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include <entt/entt.hpp>

#include "core/Config.h"
#include "core/GameDefs.h"
#include "core/Random.h"
#include "sim/SpatialHash.h"
#include "sim/components.h"
#include "sim/systems/MovementSystem.h"
#include "world/Map.h"
#include "world/MapDefinition.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 与 test_mountain.cpp 的 MountainWorld 同构：自建 Map/registry/MoveContext，MockRng 控制骰子。
struct RiverWorld {
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

    RiverWorld(int width, int height, TilingType tiling = TilingType::Square)
        : cfg(lwtest::loadCfg()) {
        cfg.map.tiling = tilingName(tiling);
        if (tiling == TilingType::Square) {
            cfg.map.width = width;
            cfg.map.height = height;
            map.configure(cfg.map);
        } else {
            chooseTableDomain(static_cast<int>(tiling), width, height, width, height);
            map.configureCanonical(tiling, width, height);
        }
        map.setTerrain(cfg.terrain);
        map.setCityConfig(cfg.city);
        factions.resize(static_cast<size_t>(kFactionTotal));
        for (int id = 0; id < kFactionTotal; ++id)
            factions[static_cast<size_t>(id)].initFromDef(cfg.factions[static_cast<size_t>(id)],
                                                          cfg);
        allLand();
    }

    // 全图陆地且全部归势力 1（避免无关的征服 RNG 干扰）；随后按需覆盖个别格。
    void allLand() {
        MapDefinition definition;
        definition.cols = map.geom().cols;
        definition.rows = map.geom().rows;
        definition.tiling = map.geom().type;
        definition.terrain.assign(static_cast<std::size_t>(map.cellCount()), MapTerrain::Land);
        std::string error;
        EXPECT_TRUE(map.loadFromDefinition(definition, &error)) << error;
        for (int idx = 0; idx < map.cellCount(); ++idx) map.atIndex(idx).belongi = 1;
    }
};

MoveContext makeCtx(RiverWorld& w) {
    return MoveContext{w.map,    w.factions, w.rng,   w.pending, w.deaths,
                       w.reg,    w.hash,     w.goSeaProb, w.ttime, w.cfg};
}

entt::entity addArmy(RiverWorld& w, double x, double y, int fid, ArmyType type, double angle,
                     double speed, bool onland = true, bool inMountain = false) {
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

void moveOnce(RiverWorld& w, entt::entity e) {
    auto ctx = makeCtx(w);
    MovementSystem::moveArmy(ctx, e);
}

// 方形 5×5：格 (1,1) 索引 6，右邻 (2,1) 索引 7 = 边 3。
constexpr int kSrc = 6;   // (1,1)
constexpr int kDst = 7;   // (2,1)
constexpr int kRiverEdge = 3;

TEST(RiverMove, FailingRollBouncesWithoutEnteringOrConquering) {
    RiverWorld w(5, 5);
    w.map.atIndex(kDst).belongi = 2;  // 敌方目标格：河骰失败必须不占领
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    w.rng.results = {false};  // 河骰失败
    moveOnce(w, e);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);   // 未减速
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);           // 反弹回原格，未进入
    EXPECT_EQ(w.map.atIndex(kDst).belongi, 2);                // 未占领
    // 反弹角：+x 撞竖边 → ≈ π（含随机小偏置）。
    EXPECT_NEAR(std::fabs(w.reg.get<comp::Velocity>(e).angle), kPi, 0.02);
}

TEST(RiverMove, PassingRollEntersWithoutSlowing) {
    RiverWorld w(5, 5);
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    w.rng.results = {true};   // 河骰通过
    w.rng.units = {0.5};      // 抖动取样（0.5 → 0 偏置）
    moveOnce(w, e);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);   // 通过不减速（对比山地 ×0.5）
    // 二期反馈：过河停顿——本 tick 停在河边（x≈2.0 + 内侧微调），剩余 0.2 结转到下一 tick。
    const auto& pos = w.reg.get<comp::Position>(e);
    EXPECT_GT(pos.x, 2.0);
    EXPECT_LT(pos.x, 2.1);
    ASSERT_TRUE(w.reg.all_of<comp::MoveCarry>(e));
    EXPECT_NEAR(w.reg.get<comp::MoveCarry>(e).value, 0.2, 1e-9);
    EXPECT_TRUE(w.reg.get<comp::OnLand>(e).value);
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);
    // 归自己 → 不触发征服骰（RNG 只消耗河骰 + 一次过河抖动）。
    EXPECT_EQ(w.rng.next, 1u);
    EXPECT_EQ(w.rng.nextUnit, 1u);
    // 下一 tick 起走 = 结转的 0.2（不补满），两 tick 位移合计恰好一份 speed（0.1 + 0.2 = 0.3）。
    moveOnce(w, e);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 2.22, 1e-9);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::MoveCarry>(e).value, 0.0);
    EXPECT_EQ(w.rng.next, 1u);  // 没有重复触发同一条河的河骰（位置已推入目标格内侧）
}

TEST(RiverMove, RiverThenMountainOrderAndNoConquerOnMountainFail) {
    RiverWorld w(5, 5);
    w.map.atIndex(kDst).mountain = true;
    w.map.atIndex(kDst).belongi = 2;
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    w.rng.results = {true, false};  // 河通过 → 山骰失败
    moveOnce(w, e);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);   // 未进山 → 未减速
    EXPECT_FALSE(w.reg.get<comp::MountainState>(e).inMountain);
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);           // 反弹
    EXPECT_EQ(w.map.atIndex(kDst).belongi, 2);                // 山骰失败不占领
    EXPECT_EQ(w.rng.next, 2u);                                // 恰好两骰：河 → 山
}

TEST(RiverMove, RiverFailPreemptsMountainAndEnemyRolls) {
    RiverWorld w(5, 5);
    w.map.atIndex(kDst).mountain = true;
    w.map.atIndex(kDst).belongi = 2;
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    w.rng.results = {false};  // 河骰失败 → 立即返回，不再掷山/敌
    moveOnce(w, e);
    EXPECT_EQ(w.rng.next, 1u);                    // 只消耗河骰
    EXPECT_EQ(w.map.atIndex(kDst).belongi, 2);    // 未占领
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);
}

TEST(RiverMove, EnemyConquestSurvivesBounceAfterRiverPass) {
    RiverWorld w(5, 5);
    w.map.atIndex(kDst).belongi = 2;
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    // 先锋 bounceMult=0.4 → 用 MockRng 显式给"反弹=true"。
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::vanguard, 0.0, 0.3);
    w.rng.results = {true, true};  // 河通过 → 敌方格反弹
    moveOnce(w, e);
    EXPECT_EQ(w.map.atIndex(kDst).belongi, 1);                 // 反弹也保留占领
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);            // 已弹回
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, 0.3);    // 未减速
}

TEST(RiverMove, NoRiverEdgeConsumesNoRngAndKeepsBehaviour) {
    RiverWorld w(5, 5);
    // 不设河：整条路径跨边 3 也不掷河骰（对照 R9 无河地图 RNG 流不变）。
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    moveOnce(w, e);
    EXPECT_EQ(w.rng.next, 0u);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 2.2, 1e-9);
}

TEST(RiverMove, TiledPathUsesCrossedEdge) {
    // 密铺路径（六）：河骰必须用 crossEdge 返回的边序号，与方形同一 processEnteredCell。
    RiverWorld w(5, 5, TilingType::Hex);
    const TilingGeom& g = w.map.geom();
    const int a = g.cellIndexAt(2, 2, 0);
    ASSERT_GE(a, 0);
    int b = -1;
    for (int k = 0; k < g.neighborCount(a); ++k)
        if (g.neighbor(a, k) >= 0) {
            b = k;
            break;
        }
    ASSERT_GE(b, 0);
    const int dst = g.neighbor(a, b);
    ASSERT_GE(dst, 0);
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{a, b}}, &error)) << error;

    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    g.cellCenter(a, ax, ay);
    g.cellCenter(dst, bx, by);
    const double angle = std::atan2(by - ay, bx - ax);
    const double dist = std::hypot(bx - ax, by - ay);
    const double speed = 0.75 * dist;  // 越过该边后仍在本 tick 内、但不足以再跨一条边

    // 失败 → 反弹留在原格
    {
        auto e = addArmy(w, ax, ay, 1, ArmyType::normal, angle, speed);
        w.rng.results = {false};
        moveOnce(w, e);
        EXPECT_TRUE(w.reg.get<comp::MountainState>(e).inMountain == false);
        EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, speed);
        EXPECT_LT(w.reg.get<comp::Position>(e).x, (ax + bx) * 0.5);  // 未越过边
        EXPECT_EQ(w.map.atIndex(dst).belongi, 1);
    }
    // 通过 → 进入邻格（无减速）
    {
        RiverWorld w2(5, 5, TilingType::Hex);
        const TilingGeom& g2 = w2.map.geom();
        ASSERT_TRUE(w2.map.setRiversFromDefinition({{a, b}}, &error)) << error;
        double ax2 = 0.0, ay2 = 0.0, bx2 = 0.0, by2 = 0.0;
        g2.cellCenter(a, ax2, ay2);
        g2.cellCenter(dst, bx2, by2);
        const double angle2 = std::atan2(by2 - ay2, bx2 - ax2);
        const double speed2 = 0.75 * std::hypot(bx2 - ax2, by2 - ay2);
        auto e = addArmy(w2, ax2, ay2, 1, ArmyType::normal, angle2, speed2);
        w2.rng.results = {true};
        moveOnce(w2, e);
        EXPECT_DOUBLE_EQ(w2.reg.get<comp::Speed>(e).value, speed2);
        const auto& pos = w2.reg.get<comp::Position>(e);
        EXPECT_EQ(g2.worldToCell(pos.x, pos.y), dst);
    }
}

TEST(RiverMove, PerUnitMultiplierScalesChance) {
    // 每兵种乘数参与概率：crossChance=0、乘数>0 → 仍为 0；反向验证乘数被读取。
    RiverWorld w(5, 5);
    w.cfg.river.crossChance = 0.0;
    w.cfg.units[static_cast<int>(ArmyType::normal)].riverCrossMult = 5.0;
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    // 真实 Rng（MockRng 忽略概率）：概率 clamp(0×5)=0 → 河骰恒失败。
    Rng realRng(7);
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    {
        auto ctx = makeCtx(w);
        ctx.rng = realRng;
        MovementSystem::moveArmy(ctx, e);
    }
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);  // 概率 0 → 必反弹
    // Config::validate 拒绝越界概率与负乘数。
    Config bad = lwtest::loadCfg();
    bad.river.crossChance = 1.5;
    std::string err;
    EXPECT_FALSE(bad.validate(&err));
    Config bad2 = lwtest::loadCfg();
    bad2.units[0].riverCrossMult = -0.5;
    EXPECT_FALSE(bad2.validate(&err));
}

// ---- 二期反馈：过河方向抖动 + 停顿结转 ----

// 方向抖动只在真正进入目标格时施加一次（对称区间，一次 unit()）。
TEST(RiverMove, CrossingJittersDirectionOnceOnEntry) {
    RiverWorld w(5, 5);
    w.cfg.river.crossAngleJitterRad = 0.4;
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    w.rng.results = {true};       // 河骰通过
    w.rng.units = {1.0};          // unit()=1 → 抖动 = (2·1-1)·0.4 = +0.4
    moveOnce(w, e);
    EXPECT_NEAR(w.reg.get<comp::Velocity>(e).angle, 0.4, 1e-12);
    EXPECT_EQ(w.rng.nextUnit, 1u);
}

// 河骰通过后被山骰弹回 → 不算进入：不抖动、不停顿、无结转（只消耗反弹自身的抖动）。
TEST(RiverMove, RiverPassButMountainBounceAppliesNoJitterOrPause) {
    RiverWorld w(5, 5);
    w.cfg.river.crossAngleJitterRad = 0.4;
    w.map.atIndex(kDst).mountain = true;
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.3);
    w.rng.results = {true, false};  // 河通过 → 山失败
    w.rng.units = {0.5, 0.5};       // 反弹抖动取样（不应再有过河抖动）
    moveOnce(w, e);
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);              // 弹回原格
    EXPECT_NEAR(w.reg.get<comp::MoveCarry>(e).value, 0.0, 1e-12);  // 无过河停顿结转
    EXPECT_EQ(w.rng.nextUnit, 1u);  // 只有反弹自身的小抖动；过河抖动未施加
}

// 河骰通过但敌方格反弹（已占领）→ 同样不算"真正进入"，不抖动不停顿。
TEST(RiverMove, RiverPassButEnemyBounceAppliesNoJitterOrPause) {
    RiverWorld w(5, 5);
    w.cfg.river.crossAngleJitterRad = 0.4;
    w.map.atIndex(kDst).belongi = 2;
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::vanguard, 0.0, 0.3);
    w.rng.results = {true, true};   // 河通过 → 敌方格反弹
    w.rng.units = {0.5, 0.5};       // 反弹抖动取样（不应再有过河抖动）
    moveOnce(w, e);
    EXPECT_EQ(w.map.atIndex(kDst).belongi, 1);                     // 已占领
    EXPECT_LT(w.reg.get<comp::Position>(e).x, 2.0);                // 弹回原格
    EXPECT_NEAR(w.reg.get<comp::MoveCarry>(e).value, 0.0, 1e-12);  // 无停顿结转
    EXPECT_EQ(w.rng.nextUnit, 1u);  // 只有反弹自身的小抖动
}

// 停顿：本 tick 停在河边并结转剩余量；下一 tick 起始 = 结转量（不补满），两 tick 合计一份 speed。
TEST(RiverMove, PauseStopsAtBankAndCarriesRemainder) {
    RiverWorld w(5, 5);
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{kSrc, kRiverEdge}}, &error)) << error;
    auto e = addArmy(w, 1.9, 1.9, 1, ArmyType::normal, 0.0, 0.5);
    w.rng.results = {true};
    w.rng.units = {0.5};  // 抖动取样（0.5 → 0 偏置，保持 +x 方向便于断言）
    moveOnce(w, e);
    const auto& pos = w.reg.get<comp::Position>(e);
    EXPECT_GT(pos.x, 2.0);   // 已推入目标格内侧（避免下一 tick 重复触发同一条河）
    EXPECT_LT(pos.x, 2.1);   // 但停在河边，没有继续走完剩余的 0.4
    EXPECT_NEAR(w.reg.get<comp::MoveCarry>(e).value, 0.4, 1e-9);
    EXPECT_EQ(w.rng.next, 1u);
    // 第二 tick：只走结转的 0.4（合计 0.1 + 0.4 = 0.5 = 一份 speed），且不重复掷河骰。
    moveOnce(w, e);
    EXPECT_NEAR(w.reg.get<comp::Position>(e).x, 2.42, 1e-9);  // 2.02（边+内侧） + 0.4（结转）
    EXPECT_DOUBLE_EQ(w.reg.get<comp::MoveCarry>(e).value, 0.0);
    EXPECT_EQ(w.rng.next, 1u);
    EXPECT_EQ(w.rng.nextUnit, 1u);
}

// 密铺（六）路径同样有抖动 + 停顿，且位置被推入目标格（不重复触发同一条河）。
TEST(RiverMove, TiledCrossingPausesAndCarries) {
    RiverWorld w(5, 5, TilingType::Hex);
    const TilingGeom& g = w.map.geom();
    const int a = g.cellIndexAt(2, 2, 0);
    ASSERT_GE(a, 0);
    int k = -1;
    for (int i = 0; i < g.neighborCount(a); ++i)
        if (g.neighbor(a, i) >= 0) {
            k = i;
            break;
        }
    ASSERT_GE(k, 0);
    const int dst = g.neighbor(a, k);
    ASSERT_GE(dst, 0);
    std::string error;
    ASSERT_TRUE(w.map.setRiversFromDefinition({{a, k}}, &error)) << error;

    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    g.cellCenter(a, ax, ay);
    g.cellCenter(dst, bx, by);
    const double angle = std::atan2(by - ay, bx - ax);
    const double dist = std::hypot(bx - ax, by - ay);
    const double speed = 0.75 * dist;
    auto e = addArmy(w, ax, ay, 1, ArmyType::normal, angle, speed);
    w.rng.results = {true};
    w.rng.units = {0.5};  // 抖动取样（0.5 → 0 偏置）
    moveOnce(w, e);
    EXPECT_DOUBLE_EQ(w.reg.get<comp::Speed>(e).value, speed);  // 过河不减速
    const auto& pos = w.reg.get<comp::Position>(e);
    EXPECT_EQ(g.worldToCell(pos.x, pos.y), dst);  // 已推入目标格，不会重复触发同一条河
    const double carry = w.reg.get<comp::MoveCarry>(e).value;
    EXPECT_GT(carry, 0.0);
    EXPECT_LT(carry, speed);
    EXPECT_EQ(w.rng.next, 1u);
    EXPECT_EQ(w.rng.nextUnit, 1u);
    moveOnce(w, e);
    EXPECT_EQ(w.rng.next, 1u);  // 第二 tick 不重复掷河骰
}

}  // namespace
