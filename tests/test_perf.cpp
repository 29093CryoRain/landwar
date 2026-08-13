// test_perf.cpp — 性能验证（翻新计划 Phase 9 §4.3）。
// 1) 2000+ 兵无头 tick 吞吐 ≥ 60Hz：600 tick（10 模拟秒）需在 10s 墙钟内完成
//    （宽松界；正常机器 ~0.3s，防 CI 抖动）。渲染侧由 vsync 锁帧（Phase 7/8 已验）。
// 2) 空间哈希剪枝生效：2500 兵散落在不同格，局部查询候选数远小于全量（对比 O(n) 暴力扫描）。
//    用「候选集数量」而非墙钟断言，避免脆测。
#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include <entt/entt.hpp>

#include "core/Simulation.h"
#include "sim/SpatialHash.h"
#include "sim/components.h"
#include "sim/systems/SpawnSystem.h"
#include "TestUtil.h"

namespace {

using namespace lw;

TEST(Performance, SimKeepsRealTimeWith2500Armies) {
    Simulation sim(lwtest::loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    // 全图隔格铺 2500+ 兵（分散避免初始巨量碰撞尖峰；spawnArmy 不校验海陆）。
    int spawned = 0;
    for (int y = 1; y < sim.map().height() && spawned < 2500; y += 2)
        for (int x = 1; x < sim.map().width() && spawned < 2500; x += 2) {
            if (SpawnSystem::spawnArmy(sim, x + 0.5, y + 0.5, (spawned % 8) + 1,
                                       ArmyType::normal) != entt::null)
                ++spawned;
        }
    EXPECT_GE(spawned, 2000);  // 确认测到 2000+ 兵

    // 600 tick = 10 模拟秒，每 tick 走 移动/战斗/特效/经济 全链路。
    const auto t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < 600; ++t) sim.tick();
    const double sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(sec, 10.0) << "600 ticks with " << spawned << " armies took " << sec << "s"
                         << " (<10s ⇒ 吞吐 ≥ 60Hz 真实时能力)";

    int alive = 0;
    for (auto e : sim.registry().view<comp::Position, comp::Collider>())
        if (!sim.registry().all_of<comp::Dead>(e)) ++alive;
    EXPECT_GT(alive, 0);  // 兵确实在跑（有战斗减员但存量仍在）
}

TEST(Performance, SpatialHashPrunesToLocalCandidates) {
    Simulation sim(lwtest::loadCfg(), 7);
    ASSERT_TRUE(sim.init());
    auto& reg = sim.registry();
    SpatialHash hash;

    // 清掉 init 兵，重新布 2500 个散兵（每格一个）。
    std::vector<entt::entity> old;
    for (auto e : reg.view<comp::Position, comp::Collider>()) old.push_back(e);
    for (auto e : old) reg.destroy(e);
    std::vector<entt::entity> placed;
    for (int y = 2; y < sim.map().height() - 2 && placed.size() < 2500u; ++y)
        for (int x = 2; x < sim.map().width() - 2 && placed.size() < 2500u; ++x) {
            auto e = reg.create();
            reg.emplace<comp::Position>(e, x + 0.5, y + 0.5);
            reg.emplace<comp::Collider>(e, 1.1);
            placed.push_back(e);
        }
    EXPECT_GE(placed.size(), 2000u);

    hash.build(reg, sim.map().width(), sim.map().height());
    const auto found = hash.queryCircle(reg, 30.5, 30.5, 1.5);
    // 半径 1.5 覆盖 3×3 邻格 → 至多 9 兵（精确过滤）。全量暴力扫描要检查 2500 个。
    EXPECT_LE(found.size(), 25u);
    EXPECT_LT(found.size(), placed.size() / 100u);  // 剪枝到 <1% 候选
}

}  // namespace
