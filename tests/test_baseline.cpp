// test_baseline.cpp — 确定性回归基线（2026-08 工程改进）。
// 锁死官方基线 `--headless --seed 42 --ticks 20000 --summary` 的 state_hash（方/六/三角各一条）：
// 任何模拟行为变化（RNG 序列/移动/战斗/经济/科技…）都会改变哈希 → 本测试失败。
//
// 更新流程（行为有意变更时）：
//   1. 重跑 build/landwar.exe --headless [--tiling hex|tri] --seed 42 --ticks 20000 --summary 取新 hash；
//   2. 改下方期望值，并在 .docs/开发计划.md §0 基线记录同步注明原因。
//
// 注意：本测试跑 20000 tick × 3 密铺（Debug 下共约 3 分钟），是全套单测最慢的组，属预期
// （确定性红线的机器强制，替代人肉跑 run_test.bat）。
//
// 2026-08-17（调试期）：`Map::sampleCityLevel` 暂改**均匀分布**（幂律在 levels[0]≠1 时概率
// 异常，用户指示先确认密铺系统正常、日后再恢复）→ 城市等级分布改变 → 方/六/三基线 hash
// 全部漂移。**两个 Determinism 测试暂时禁用（DISABLED_ 前缀）**：等用户下令恢复幂律并
// 确认密铺系统后，重跑 CLI 取新 hash 更新本文件与 .docs/开发计划.md §0。
// 2026-08-24：Config 新增 render.tile.variation（双色密铺分档，纯渲染）→ config 序列化
// 改变 → 方/六/三基线 hash 全部漂移（sim 行为不变：square land 1758/hex 2291/tri 3131）。
// 2026-08-24：Laves4612 新增 L6 矩形城市变体（同级多种形状）→ config 序列化改变 + 若地图出现
// 6 级城则其变体选择消耗 RNG → 方/六/三基线 hash 再漂移（square land 仍 1758，sim 同旧分布）。
// 2026-08-24：Laves4612 城市形状表按新几何重写（上一版偏移对不上新 spec，城市乱套；本版
// 按文档 2/4/4/6/6/12/12 重构并逐 base 验证解析唯一）→ config 序列化改变 → 基线 hash 再漂移
//（sim 行为不变：square land 1758/hex 2291/tri 3131）。
// 2026-08-25：补齐 arch/laves 3.3.3.3.6、3.3.4.3.4 城市形状表 + laves_4612 重写（锚格入形状、
// 方向锚定）→ config 序列化改变 → 基线 hash 再漂移（sim 行为不变：square land 1758/hex 2291/tri 3131）。
// 已重跑 CLI 更新（square 0x63ae8874dea10409、hex 0x3dfaffd9260c5441、tri 0x3d29e92db2e569ac）。
// 2026-08-26：城市形状表外置 data/city_shapes.json；锚限制重构——ShapeCell 去 orient、
// anchorN 删除（与掩码功能重叠，改由 baseGroups/anchorBases 组名列表承担）、掩码序列化
// 改 anchorBases 数组 → config 序列化改变 → 基线 hash 再漂移（sim 行为不变：landCount
// 断言同旧值）。重跑更新：
// 2026-08-26 方案A：city_shapes.json 烘入世界坐标（删运行时 (dx,dy)→(-dy,dx)）→ 序列化再变，
// sim 行为不变（landCount 同旧值）。square 0xdc3706c74b5cd67d、hex 0x284b259903d14665、tri 0x2138e03533eb2c08。
#include <gtest/gtest.h>

#include "core/Simulation.h"
#include "replay/Headless.h"
#include "replay/Snapshot.h"
#include "world/MapGenerator.h"
#include "TestUtil.h"

namespace {

// 随机地图确定性基线（2026-08-26 用户定夺）：不用预装 BMP，改按固定随机地图种子 42 生成
// 一张 120×120 随机图（长=宽=120；对六/三角取各自 <=120 的合规最大值，此处 120 已偶），
// 陆地占比 0.5、强制边缘为海、山密度 0.1、城密度 0.015，主种子 42（全默认 AI），跑 2500 tick
// → state_hash。任何模拟行为变化（RNG 序列/移动/战斗/经济/科技/城市形状/几何…）都会改变哈希。
// 更新流程（行为有意变更时）：改下方 hash 期望值，并在 .docs/开发计划.md §0 基线记录同步注明原因。
std::uint64_t runRandomBaseline(lw::TilingType t, int ticks = 2500) {
    lw::Config cfg = lwtest::loadCfg();
    cfg.map.tiling = lw::tilingName(t);
    lw::MapGenParams gp{120, 120, 0.5, 0.1, 0.015, 0.3, /*forceCoast=*/true, t};
    const std::string path = lw::MapGenerator::defaultPath(42, gp);
    EXPECT_TRUE(lw::MapGenerator::generate(path, 42, gp));
    if (gp.height & 1) --gp.height;  // 六/三角行数强制偶（与生成器/Map::configure 一致）
    cfg.map.width = gp.width;
    cfg.map.height = gp.height;
    cfg.map.file = path;
    lw::Simulation sim(cfg, 42, 42);
    EXPECT_TRUE(sim.init());
    for (int i = 0; i < ticks; ++i) sim.tick();
    return lw::fnv1a64(lw::Snapshot::serialize(sim));
}

TEST(Determinism, BaselineSeed42_2500Ticks_RandomMap_StateHash) {
    EXPECT_EQ(runRandomBaseline(lw::TilingType::Square, 2500), 0xb3d13d5ebdd1b1cdull)
        << "square 随机图基线漂移";
}

// P12：六/三角各自基线（密铺几何 + 移动/特效路径独立于方形）。同随机图参数（种子 42）。
TEST(Determinism, BaselineHexTri_2500Ticks_RandomMap_StateHash) {
    EXPECT_EQ(runRandomBaseline(lw::TilingType::Hex, 2500), 0x8caa8fb275764ac8ull)
        << "hex 随机图基线漂移";
    EXPECT_EQ(runRandomBaseline(lw::TilingType::Tri, 2500), 0xecf9ba0b68d5ad39ull)
        << "tri 随机图基线漂移";
}

// 全密铺确定性小参数快测：地图小、城密度低、tick 少。只为锁“同 seed 必同 hash”。
TEST(Determinism, SmallTiledAllTilingsSameSeedSameHash) {
    const std::vector<lw::TilingType> tilings = {
        lw::TilingType::Hex,       lw::TilingType::Tri,
        lw::TilingType::Arch33336, lw::TilingType::Arch33434,
        lw::TilingType::Arch3464,  lw::TilingType::Arch3636,
        lw::TilingType::Arch31212, lw::TilingType::Arch4612,
        lw::TilingType::Arch488,   lw::TilingType::Laves3636,
        lw::TilingType::Laves31212, lw::TilingType::Laves4612,
        lw::TilingType::Laves488,  lw::TilingType::Laves33434,
        lw::TilingType::Laves33336, lw::TilingType::Laves3464};
    for (lw::TilingType t : tilings) {
        lw::Config cfg = lwtest::loadCfg();
        cfg.map.tiling = lw::tilingName(t);
        lw::MapGenParams gp{32, 32, 0.40, 0.05, 0.01, 0.3, false, t};
        const std::string path = lw::MapGenerator::defaultPath(777, gp);
        ASSERT_TRUE(lw::MapGenerator::generate(path, 777, gp));
        if (gp.height & 1) --gp.height;
        cfg.map.width = gp.width;
        cfg.map.height = gp.height;
        cfg.map.file = path;
        lw::Simulation a(cfg, 777, 777);
        ASSERT_TRUE(a.init());
        lw::Simulation b(cfg, 777, 777);
        ASSERT_TRUE(b.init());
        for (int i = 0; i < 1000; ++i) { a.tick(); b.tick(); }
        EXPECT_EQ(lw::fnv1a64(lw::Snapshot::serialize(a)),
                  lw::fnv1a64(lw::Snapshot::serialize(b)))
            << "tiling " << lw::tilingName(t) << " 同 seed 哈希不一致";
    }
}

}  // namespace
