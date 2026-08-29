// test_baseline.cpp — 确定性回归基线（2026-08 工程改进，2026-08-27 改为“语义基线”）。
// 不再使用完整 Snapshot 序列化 hash（避免 config/快照字段增删导致频繁漂移），
// 只锁“核心可玩状态”：每格势力归属 + 所有兵的类型/势力/位置/速度方向/速度。
// 方/六/三角各一条；任何影响这些状态的模拟行为变化都会改变哈希。
//
// 更新流程（行为有意变更时）：
//   1. 重跑本测试取新 hash；
//   2. 改下方期望值，并在 .docs/开发计划.md §0 基线记录同步注明原因。
//
// 注意：本测试跑 2500 tick × 3 密铺，是全套单测较慢的组，属预期。
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
// 2026-08-27 P1.1：config 移除 map.file/width/height 三键（地图改菜单选择；Config::Map 保留代码默认）
// → config 序列化变（sim 行为不变）→ 基线再漂移。square 0x328b3f271993ee15、hex 0xf2c8d5f4182ae4d2、
// tri 0xc406ed79ef5aea47。
// 2026-08-27 P1.2：city_shapes.json 加入 square/hex/tri（cells 世界偏移）+ config 删除 render.city.iconScale。
// 同日起基线从“完整快照 hash”改为“语义基线”（每格归属 + 兵核心状态），不再因 config 序列化
// 变化而漂移。当前语义基线：square 0x8097e7a69627f105、hex 0x9caec20d03672e32、
// tri 0x8e8ab02d4e1f7113。
// 2026-08-29 P2.2：比例映射扩展到 square/hex/tri；hex 120×120 输入改为 117×126 周期域。
// P3.1：反弹抖动改为一次 unit() 采样，P3.2：输入尺寸改为四舍五入到最近倍数；更新语义基线。
#include <gtest/gtest.h>

#include <sstream>

#include "core/Simulation.h"
#include "replay/Headless.h"
#include "replay/Snapshot.h"
#include "sim/components.h"
#include "world/MapGenerator.h"
#include "TestUtil.h"

namespace {

// 语义基线：只取“每格归属 + 所有兵的核心状态”，避免 config 序列化/快照扩展造成不必要漂移。
std::uint64_t semanticBaselineHash(const lw::Simulation& sim) {
    std::ostringstream oss;
    const auto& map = sim.map();
    for (int idx = 0; idx < map.cellCount(); ++idx)
        oss << map.atIndex(idx).belongi << ',';
    const auto& reg = sim.registry();
    if (const auto* ps = reg.storage<lw::comp::Position>(); ps) {
        for (auto elem : ps->reach()) {
            const auto e = std::get<0>(elem);
            if (!reg.all_of<lw::comp::UnitType>(e)) continue;  // 只统计兵，不含子弹/特效
            const auto& p = reg.get<lw::comp::Position>(e);
            const auto& v = reg.get<lw::comp::Velocity>(e);
            const auto& sp = reg.get<lw::comp::Speed>(e);
            const auto& f = reg.get<lw::comp::FactionId>(e);
            const auto& u = reg.get<lw::comp::UnitType>(e);
            oss << static_cast<int>(u.type) << ',' << f.value << ',' << p.x << ',' << p.y << ','
                << v.angle << ',' << sp.value << ';';
        }
    }
    return lw::fnv1a64(oss.str());
}

// 随机地图确定性基线（2026-08-26 用户定夺）：不用预装 BMP，改按固定随机地图种子 42 生成
// 一张按用户长宽 120×120 请求的随机图（hex/tri 经比例映射后分别为 117×140、80×90），
// 陆地占比 0.5、强制边缘为海、山密度 0.1、城密度 0.015，主种子 42（全默认 AI），跑 2500 tick
// → semanticBaselineHash。
// 更新流程（行为有意变更时）：改下方 hash 期望值，并在 .docs/开发计划.md §0 基线记录同步注明原因。
std::uint64_t runRandomBaseline(lw::TilingType t, int ticks = 2500) {
    lw::Config cfg = lwtest::loadCfg();
    cfg.map.tiling = lw::tilingName(t);
    lw::MapGenParams gp{120, 120, 0.5, 0.1, 0.015, 0.3, /*forceCoast=*/true, t};
    const std::string path = lw::MapGenerator::defaultPath(42, gp);
    EXPECT_TRUE(lw::MapGenerator::generate(path, 42, gp));
    cfg.map.width = gp.width;
    cfg.map.height = gp.height;
    cfg.map.file = path;
    lw::Simulation sim(cfg, 42, 42);
    EXPECT_TRUE(sim.init());
    for (int i = 0; i < ticks; ++i) sim.tick();
    return semanticBaselineHash(sim);
}

TEST(Determinism, BaselineSeed42_2500Ticks_RandomMap_StateHash) {
    EXPECT_EQ(runRandomBaseline(lw::TilingType::Square, 2500), 0x59826e3aecdb51b1ull)
        << "square 随机图基线漂移";
}

// P12：六/三角各自基线（密铺几何 + 移动/特效路径独立于方形）。同随机图参数（种子 42）。
TEST(Determinism, BaselineHexTri_2500Ticks_RandomMap_StateHash) {
    EXPECT_EQ(runRandomBaseline(lw::TilingType::Hex, 2500), 0x8b595f2591f03127ull)
        << "hex 随机图基线漂移";
    EXPECT_EQ(runRandomBaseline(lw::TilingType::Tri, 2500), 0x374b0972e2a4ed25ull)
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
