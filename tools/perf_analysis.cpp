// perf_analysis.cpp — 无头性能剖析工具（临时，2026-08）。
// 用法：landwar_perf [--tiling square|hex|tri] [--ticks N] [--w N] [--h N] [--seed N]
// 作用：按指定密铺生成（或加载）~w×h 随机地图 → 构建全部默认 AI 的 Simulation →
//       enableProfiling + 步进 N tick → 按阶段打印墙钟耗时占比（定位最耗时功能）。
// 纯计时不消耗 RNG/改状态，不影响确定性。
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include <spdlog/spdlog.h>

#include "core/Config.h"
#include "core/GameDefs.h"
#include "core/Paths.h"
#include "core/Simulation.h"
#include "world/MapGenerator.h"

namespace {

struct Opts {
    std::string tiling = "square";
    int ticks = 4000;
    int w = 120;
    int h = 120;
    std::uint32_t seed = 42;
};

Opts parse(int argc, char* argv[]) {
    Opts o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--tiling") o.tiling = next();
        else if (a == "--ticks") o.ticks = std::stoi(next());
        else if (a == "--w") o.w = std::stoi(next());
        else if (a == "--h") o.h = std::stoi(next());
        else if (a == "--seed") o.seed = static_cast<std::uint32_t>(std::stoul(next()));
    }
    return o;
}

const char* stageName(lw::SimStage s) {
    switch (s) {
        case lw::SimStage::Movement: return "Movement";
        case lw::SimStage::PendingSpawn: return "PendingSpawn";
        case lw::SimStage::UnitAction: return "UnitAction";
        case lw::SimStage::Projectile: return "Projectile";
        case lw::SimStage::Economy: return "Economy";
        case lw::SimStage::Production: return "Production";
        case lw::SimStage::Effect: return "Effect";
        case lw::SimStage::Death: return "Death";
        case lw::SimStage::Capital: return "Capital";
        case lw::SimStage::Tech: return "Tech";
        case lw::SimStage::MiscTick: return "MiscTick";
        case lw::SimStage::Detect: return "Detect";
        default: return "?";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    lw::ensureUserDataDirs();
    const Opts o = parse(argc, argv);
    const auto tiling = lw::tilingFromName(o.tiling);

    // 生成 ~o.w×o.h 随机地图（方=BMP、六/三=lwmap）；与 Headless 同参数集。
    lw::MapGenParams gp;
    gp.width = o.w;
    gp.height = o.h;
    gp.seaRatio = 0.40;
    gp.mountainDensity = 0.08;
    gp.cityDensity = 0.02;
    gp.tiling = tiling;

    lw::Config cfg = lw::Config::loadFromFile(lw::kDefaultConfigPath);
    cfg.map.tiling = o.tiling;
    // 生成器内部 clamp（六/三偶数行、三列减半），这里把"视觉宽高"原样交给 generate/configure，
    // 两者用同一公式推导实际 cols/rows（lwmap 头尺寸对齐）。只保证偶数行（偶数输入恒不改）。
    gp.width = std::clamp(gp.width, 32, 200);
    gp.height = std::clamp(gp.height, 32, 200);
    if ((tiling == lw::TilingType::Hex || tiling == lw::TilingType::Tri) && (gp.height & 1))
        --gp.height;

    const std::string mpath = lw::MapGenerator::defaultPath(o.seed, gp);
    if (!lw::MapGenerator::generate(mpath, o.seed, gp)) {
        spdlog::error("map generate failed: {}", mpath);
        return 1;
    }
    cfg.map.width = gp.width;
    cfg.map.height = gp.height;
    cfg.map.file = mpath;

    lw::Simulation sim(cfg, o.seed, o.seed);
    if (!sim.init()) {
        spdlog::error("simulation init failed");
        return 1;
    }
    // 默认 Options（全势力登场、全 AI）——与头less runHeadless 一致。
    sim.enableProfiling();

    // 预热：首若干 tick 可能含初始大量征服/爆炸尖峰，直接测 0..ticks 全量即可（反映整体）。
    for (int t = 0; t < o.ticks; ++t) sim.tick();

    const auto& pr = sim.profile();
    std::uint64_t total = 0;
    std::cout << "ticks=" << o.ticks << " tiling=" << o.tiling << " map=" << gp.width << "x"
              << gp.height << " seed=" << o.seed << "\n";
    std::cout << "cellCount=" << sim.map().cellCount() << "\n";
    for (int i = 0; i < static_cast<int>(lw::SimStage::Count); ++i) {
        const auto& p = pr.phases[static_cast<size_t>(i)];
        total += p.ns;
    }
    std::cout << "total=" << total << " ns\n\n";
    std::cout.fill(' ');
    for (int i = 0; i < static_cast<int>(lw::SimStage::Count); ++i) {
        const auto& p = pr.phases[static_cast<size_t>(i)];
        const double pct = total ? 100.0 * static_cast<double>(p.ns) / static_cast<double>(total) : 0.0;
        const double per_tick = o.ticks ? static_cast<double>(p.ns) / static_cast<double>(o.ticks) : 0.0;
        std::cout << std::left << std::setw(14) << stageName(static_cast<lw::SimStage>(i))
                  << " ns=" << std::setw(12) << p.ns << "  "
                  << std::setw(6) << std::setprecision(1) << std::fixed << pct << "%  "
                  << "ns/tick=" << std::setw(12) << (long long)per_tick << "\n";
    }

    // 另附：Movement 内部细分（hash 重建 / 排序 / 逐兵移动）。
    const double moveTotal = total ? 100.0 * static_cast<double>(pr.moveHashNs + pr.moveSortNs +
                                                                pr.moveLoopNs)
                                   / static_cast<double>(total) : 0.0;
    std::cout << "\n[inside Movement] " << std::fixed << std::setprecision(1) << moveTotal
              << "% of total\n";
    std::cout << std::left << std::setw(14) << "moveHash"
              << " ns=" << std::setw(12) << pr.moveHashNs << "  "
              << std::setw(6) << (total ? 100.0 * pr.moveHashNs / total : 0.0) << "%  "
              << "ns/tick=" << std::setw(12)
              << (o.ticks ? (long long)(pr.moveHashNs / o.ticks) : 0) << "\n";
    std::cout << std::left << std::setw(14) << "moveSort"
              << " ns=" << std::setw(12) << pr.moveSortNs << "  "
              << std::setw(6) << (total ? 100.0 * pr.moveSortNs / total : 0.0) << "%  "
              << "ns/tick=" << std::setw(12)
              << (o.ticks ? (long long)(pr.moveSortNs / o.ticks) : 0) << "\n";
    std::cout << std::left << std::setw(14) << "moveLoop"
              << " ns=" << std::setw(12) << pr.moveLoopNs << "  "
              << std::setw(6) << (total ? 100.0 * pr.moveLoopNs / total : 0.0) << "%  "
              << "ns/tick=" << std::setw(12)
              << (o.ticks ? (long long)(pr.moveLoopNs / o.ticks) : 0) << "\n";

    // 逐兵子阶段细分（moveArmy 内部：战斗 / 几何穿越 / 进入格 / 征服）。
    const auto& mv = pr.move;
    const auto emit = [&](const char* name, std::uint64_t ns) {
        const double pct = total ? 100.0 * static_cast<double>(ns) / static_cast<double>(total) : 0.0;
        std::cout << std::left << std::setw(14) << name << " ns=" << std::setw(12) << ns << "  "
                  << std::setw(6) << pct << "%  "
                  << "ns/tick=" << std::setw(12)
                  << (o.ticks ? (long long)(ns / o.ticks) : 0) << "\n";
    };
    std::cout << "\n[inside moveArmy]\n";
    emit("loop", mv.loopNs);
    emit("combat", mv.combatNs);
    emit("geom", mv.geomNs);
    emit("enter", mv.enterNs);
    emit("conquer", mv.conquerNs);
    int armiesEnd = 0;
    for (auto e : sim.registry().view<lw::comp::Position, lw::comp::Collider>())
        if (!sim.registry().all_of<lw::comp::Dead>(e)) ++armiesEnd;
    std::cout << std::left << std::setw(14) << "armies(end)" << "=" << armiesEnd << std::endl;
    return 0;
}
