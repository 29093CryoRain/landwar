// test_baseline.cpp — 确定性回归基线（2026-08 工程改进）。
// 锁死官方基线 `--headless --seed 42 --ticks 20000 --summary` 的 state_hash：
// 任何模拟行为变化（RNG 序列/移动/战斗/经济/科技…）都会改变哈希 → 本测试失败。
//
// 更新流程（行为有意变更时）：
//   1. 重跑 build/landwar.exe --headless --seed 42 --ticks 20000 --summary 取新 hash；
//   2. 改下方期望值，并在 .docs/开发计划.md §0 基线记录同步注明原因。
//
// 注意：本测试跑 20000 tick（Debug 下约 30s），是全套单测最慢的一条，属预期
// （确定性红线的机器强制，替代人肉跑 run_test.bat）。
#include <gtest/gtest.h>

#include "core/Simulation.h"
#include "replay/Headless.h"
#include "replay/Snapshot.h"
#include "TestUtil.h"

TEST(Determinism, BaselineSeed42_20000Ticks_StateHash) {
    lw::Simulation sim(lwtest::loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    for (int i = 0; i < 20000; ++i) sim.tick();
    const std::uint64_t h = lw::fnv1a64(lw::Snapshot::serialize(sim));
    // 2026-08 双色势力渲染系统并入 config（factions.secondary / render.tile /
    // render.city.mix 新键 + sea.cyanSeaMult 死键删除 + 中立主色 127→96）→ config 序列化
    // 变 → hash 更新（sim 行为不变：land=3139 army=276 与上一基线相同；见 .docs/开发计划.md §0；
    // 上一基线 0x4ac5a7925795d690 = 霰弹速度方差收窄后）。
    EXPECT_EQ(h, 0xc44cb1d121294999ull)
        << "确定性基线漂移！对应 CLI: landwar --headless --seed 42 --ticks 20000 --summary";
    // 顺带锁定终局规模（land=3139 与基线记录一致；army 数随战斗轨迹波动，不锁）。
    EXPECT_EQ(sim.faction(1).landCount + sim.faction(2).landCount + sim.faction(3).landCount +
                  sim.faction(4).landCount + sim.faction(5).landCount + sim.faction(6).landCount +
                  sim.faction(7).landCount + sim.faction(8).landCount,
              3139);
}
