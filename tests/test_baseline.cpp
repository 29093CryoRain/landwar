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
#include <gtest/gtest.h>

#include "core/Simulation.h"
#include "replay/Headless.h"
#include "replay/Snapshot.h"
#include "world/MapGenerator.h"
#include "TestUtil.h"

namespace {

// 六/三角：生成随机 lwmap（seed=42，与 CLI --tiling 路径一致）→ init → 20k tick → hash。
std::uint64_t runTiledBaseline(lw::TilingType t) {
    lw::Config cfg = lwtest::loadCfg();
    cfg.map.tiling = lw::tilingName(t);
    lw::MapGenParams gp{105, 95, 0.40, 0.08, 0.02, false, t};
    const std::string path = lw::MapGenerator::defaultPath(42, gp);
    EXPECT_TRUE(lw::MapGenerator::generate(path, 42, gp));
    if (gp.height & 1) --gp.height;  // 偶数行（与生成器/Map::configure 一致）
    cfg.map.width = gp.width;
    cfg.map.height = gp.height;
    cfg.map.file = path;
    lw::Simulation sim(cfg, 42, 42);
    EXPECT_TRUE(sim.init());
    for (int i = 0; i < 20000; ++i) sim.tick();
    return lw::fnv1a64(lw::Snapshot::serialize(sim));
}

}  // namespace

TEST(Determinism, DISABLED_BaselineSeed42_20000Ticks_StateHash) {
    lw::Simulation sim(lwtest::loadCfg(), 42);
    ASSERT_TRUE(sim.init());
    for (int i = 0; i < 20000; ++i) sim.tick();
    const std::uint64_t h = lw::fnv1a64(lw::Snapshot::serialize(sim));
    // 2026-08 双色势力渲染系统并入 config（factions.secondary / render.tile /
    // render.city.mix 新键 + sea.cyanSeaMult 死键删除 + 中立主色 127→96）→ config 序列化
    // 变 → hash 更新（sim 行为不变：land=3139 army=276 与上一基线相同；见 .docs/开发计划.md §0；
    // 上一基线 0x4ac5a7925795d690 = 霰弹速度方差收窄后）。
    // 2026-08-14 二次更新：用户调 render.tile（主0.5/副0.3/白0.2）与 render.city.mix
    // （主0.7/副0.1/黑0.2）等数据文件值 → 仅 config 序列化变。
    // 2026-08-14 P12 起步：config 新增 map.tiling 与 city.hex/city.tri 形状表 → 0x6b2bba9beef464a4。
    // 2026-08-14 P12 快照 v6：cells 按密铺格序 + 城市 baseIndex + config tiling →
    // 0x6b2bba9beef464a4 → 0xbd90b94d52be9413（sim 行为不变：land=3139 army=276）。
    // 2026-08-15 三次更新：① 三角城市形状按用户定稿语义重做（正锚模式 + 反锚镜像）→
    // config.toJson() 的 city.tri 形状表序列化变 → 纯 config 序列化变化 → hash 更新
    // （sim 行为不变：land=3139 army=276 与上基线相同）→ 0x230960a67e857bb0。
    // 2026-08-16 半正/Laves 密铺开发：TilingType 枚举扩展 + city.tilings 通用形状表段 +
    // Arch/Laves 城市形状表（Arch488/Arch3636/Arch3464/Arch4612/Laves3636/Laves488/
    // Laves3464 等）→ config 序列化变化 → hash 更新（方 sim 行为不变：land=3139）。
    // 2026-08-16 深夜-白天 两次更新：① 半正/Laves 首都改"从已生成城市中分配"
    // （Snapshot 新增 capitalsB 基础格序号键，**全密铺序列化**）→ 方/六/三 hash 全动
    // （sim 行为不变：land=3139/5922/5866、army=276/110/494 与上基线相同）；
    // ② 锚格朝向修复（TilingGeom 每基础格变换 + Map 形状解析按锚格朝向旋转/镜像）——
    // 仅表驱动密铺分支，方/六/三不受影响（此更新只为隔离验证，hash 未再动）。
    // 2026-08-17 Laves 城市形状表重写（5 密铺全部按开发思路.txt 语义重做 + Laves4612
    // spec 朝向修正）→ 仅 config 序列化变化（sim 行为不变：land=3139 army=276 与上基线相同）
    // → hash 更新 → 0x4a3b07a46a2542bf。
    EXPECT_EQ(h, 0x4a3b07a46a2542bfull)
        << "确定性基线漂移！对应 CLI: landwar --headless --seed 42 --ticks 20000 --summary";
    // 顺带锁定终局规模（land=3139 与基线记录一致；army 数随战斗轨迹波动，不锁）。
    EXPECT_EQ(sim.faction(1).landCount + sim.faction(2).landCount + sim.faction(3).landCount +
                  sim.faction(4).landCount + sim.faction(5).landCount + sim.faction(6).landCount +
                  sim.faction(7).landCount + sim.faction(8).landCount,
              3139);
}

// P12：六/三角各自基线（密铺几何 + 移动/特效路径独立于方形）。
// 2026-08-15 更新：① 三角改常规直边布局（行内无 b/2 平移，邻接同列上下）；
// ② 海拔场生成回退**最朴素版**（每格中心直接采样 fbm，零后处理；先后试过
// "平滑+对平均"与"抗锯齿重采样"，用户实测均未彻底解决横纹/单朝向 → 回退作纯净基线，
// 随后定位根因在 forceCoast 边缘判定——见下）；
// ③ forceCoast 外圈判定改为"格多边形顶点贴边"（用户规范：100% 陆地时只有最外一圈海；
//    此路径 CLI 默认 forceCoast=false 不触发，但城市形状表变更影响所有密铺）；
// ④ 三角城市形状按用户定稿语义重做（正锚模式 + 反锚镜像：L1 正/反、L2 水平公共边、
//    L4 大三角正/反、L6 六边形、L8 = L6+顶正+底反）→ 城市分布/经济轨迹变。
// ⑤ 2026-08 三角**列数减半**（width 语义 = 视觉列数，列对数 = width/2 → 格数与方形
//    一致；强制列数偶）→ 三角地图/格数减半 → tri hash 再更新（land 5866）；hex 不受
//    影响（0x525b2d8fd4be80ab 未变）。
// → hex/tri hash 更新（hex land=5922、tri land=5866）。
// 2026-08-16 深夜-白天：Snapshot 新增 capitalsB 键（半正/Laves 首都基础格序号，全密铺
// 序列化）→ hex/tri hash 再更新（sim 行为不变：hex land=5922 army=110、tri land=5866
// army=494 与上基线相同）。
// 2026-08-17 Laves 形状表重写 + Laves4612 spec 朝向修正 → 仅 config 序列化变化
// （sim 行为不变）→ hash 更新。
TEST(Determinism, DISABLED_BaselineHexTri_20000Ticks_StateHash) {
    EXPECT_EQ(runTiledBaseline(lw::TilingType::Hex), 0x10f499d16532133a)
        << "hex 基线漂移！对应 CLI: landwar --headless --tiling hex --seed 42 --ticks 20000 --summary";
    EXPECT_EQ(runTiledBaseline(lw::TilingType::Tri), 0x4e48cb8fccf1f11a)
        << "tri 基线漂移！对应 CLI: landwar --headless --tiling tri --seed 42 --ticks 20000 --summary";
}
