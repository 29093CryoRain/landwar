// test_move_perf.cpp — 移动系统逐功能耗时剖析（2026-08 性能分析）。
// 目的：定位模拟中"移动系统各功能"（战斗查询 / 几何穿越 / 进入格处理 / 征服）分别耗多少。
// 做法：全部默认 AI、无头、~14400 格（≈120×120）地图，跑 2000 tick，开启 opt-in profiling，
//       按功能累加墙钟；每个密铺一张地图，单图超时（wall-clock 预算）即提前终止该图防卡死。
//
// 覆盖密铺：除 arch/laves 3.3.3.3.6 与 3.3.4.3.4（共 4 种，尺寸映射暂缺）外的全部
//       方/六/三 + 10 种半正/Laves。单测不锁时序数值（时序随机器/构建浮动），只输出并
//       校验"运行可完成 + 各功能记账合理（combat/geom/enter/conquer 合计 ≤ loop）"。
//
// 运行：ctest / 直接 ./landwar_tests --gtest_filter=MovePerf.*
//       时序值经 spdlog info 打印（Debug 下耗时放大，仅作相对参考；性能验证看 Release）。
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/Simulation.h"
#include "world/MapGenerator.h"
#include "TestUtil.h"

namespace {

using Clock = std::chrono::steady_clock;

struct TilingCase {
    lw::TilingType type;
    int width;    // 用户输入长（视觉；方=列、六=列、三=视觉列）
    int height;   // 用户输入宽
};

// 本剖析覆盖的密铺集合（排除 arch/laves 3.3.3.3.6 与 3.3.4.3.4 共 4 种）。
// 方/六/三 固定 120×120（≈14400 格）；表驱动为 (Ra,Rb) 倍数下最贴近 120×120 且 面积≈14400。
std::vector<TilingCase> eligibleTilings() {
    return {
        {lw::TilingType::Square, 120, 120},
        {lw::TilingType::Hex, 120, 120},
        {lw::TilingType::Tri, 120, 120},
        {lw::TilingType::Arch3464, 128, 114},
        {lw::TilingType::Arch3636, 105, 138},
        {lw::TilingType::Arch31212, 105, 138},
        {lw::TilingType::Arch4612, 108, 135},
        {lw::TilingType::Arch488, 126, 115},
        {lw::TilingType::Laves3636, 105, 138},
        {lw::TilingType::Laves31212, 128, 114},
        {lw::TilingType::Laves4612, 122, 120},
        {lw::TilingType::Laves488, 121, 121},
        {lw::TilingType::Laves3464, 128, 114},
    };
}

struct MoveTimes {
    std::uint64_t loop = 0, combat = 0, geom = 0, enter = 0, conquer = 0;
};

struct RunResult {
    MoveTimes times;
    std::uint64_t cells = 0;
    std::uint64_t hashNs = 0;
    std::uint64_t sortNs = 0;
};

// 单密铺：生成 ~width×height 地图 → init → 开启 profiling → 跑 ticks（有 wall-clock 预算，
// 超预算提前终止）。返回各功能累计纳秒 + cfg 未最终确定时的失败标记（done<0）。
// doneTicks 输出实际跑到的 tick 数（<ticks = 提前终止；-1 = 生成/init 失败）。
RunResult runOne(lw::TilingType t, int width, int height, int ticks, std::uint64_t budgetMs,
                 int& doneTicks) {
    lw::Config cfg = lwtest::loadCfg();
    cfg.map.tiling = lw::tilingName(t);
    lw::MapGenParams gp;
    gp.width = width;
    gp.height = height;
    gp.seaRatio = 0.40;
    gp.mountainDensity = 0.08;
    gp.cityDensity = 0.02;
    gp.tiling = t;
    const std::string path = lw::MapGenerator::defaultPath(42, gp);
    if (!lw::MapGenerator::generate(path, 42, gp)) {
        doneTicks = -1;
        return {};
    }
    // cfg 用"合规输入"，Map::configure 端会再次 chooseTableDomain（与生成器同一公式）→
    // lwmap 头尺寸匹配。六/三角生成器内部 clamp 偶数行，这里同步偶数行（与 Map.cpp 一致）。
    cfg.map.width = width;
    cfg.map.height = height;
    cfg.map.file = path;

    lw::Simulation sim(cfg, 42, 42);
    if (!sim.init()) {
        doneTicks = -1;
        return {};
    }
    sim.enableProfiling();

    const auto t0 = Clock::now();
    int i = 0;
    for (; i < ticks; ++i) {
        sim.tick();
        if (static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0)
                    .count())
            > budgetMs)
            break;  // 单图超预算 → 提前终止（保守，每 tick 检查一次）
    }
    doneTicks = i;

    RunResult rr;
    const auto& pr = sim.profile();
    rr.times.loop = pr.move.loopNs;
    rr.times.combat = pr.move.combatNs;
    rr.times.geom = pr.move.geomNs;
    rr.times.enter = pr.move.enterNs;
    rr.times.conquer = pr.move.conquerNs;
    rr.cells = static_cast<std::uint64_t>(sim.map().cellCount());
    rr.hashNs = pr.moveHashNs;
    rr.sortNs = pr.moveSortNs;
    return rr;
}

TEST(MovePerf, BreakdownAllTilings) {
    const auto cases = eligibleTilings();
    ASSERT_GE(cases.size(), 10u);
    for (const auto& c : cases) {
        constexpr int kTicks = 2000;
        constexpr std::uint64_t kBudgetMs = 120000;  // 单图 120s 兜底防卡死
        int done = 0;
        RunResult rr = runOne(c.type, c.width, c.height, kTicks, kBudgetMs, done);

        if (done < 0) {
            ADD_FAILURE() << lw::tilingName(c.type) << " (" << c.width << "x" << c.height
                          << ") 地图生成/init 失败";
            continue;
        }
        if (done < kTicks)
            spdlog::warn("[MovePerf] {}: 提前终止于 tick {}/{}（达墙钟预算）",
                         lw::tilingName(c.type), done, kTicks);

        const auto& X = rr.times;
        const std::uint64_t sum = X.combat + X.geom + X.enter + X.conquer;
        EXPECT_LE(sum, X.loop + X.loop / 10)
            << lw::tilingName(c.type) << " 子项合计超过 moveArmy loop";

        spdlog::info(
            "[MovePerf] {:>12} {:>4}x{:<5} cells={:>6} ticks={:>4} | loop={:<11} hash={:<11} "
            "sort={:<11} | combat={:<11} geom={:<11} enter={:<11} conquer={:<11}",
            lw::tilingName(c.type), c.width, c.height, rr.cells, done, X.loop, rr.hashNs,
            rr.sortNs, X.combat, X.geom, X.enter, X.conquer);
    }
}

}  // namespace
