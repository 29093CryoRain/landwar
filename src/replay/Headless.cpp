// Headless.cpp — 无头 CLI 运行 + 终局摘要（Phase 6）。
#include "replay/Headless.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "core/Simulation.h"
#include "core/Paths.h"
#include "replay/Cli.h"
#include "replay/Snapshot.h"
#include "sim/components.h"
#include "world/MapGenerator.h"

namespace lw {

namespace {
using Clock = std::chrono::steady_clock;
}  // namespace

std::uint64_t fnv1a64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string formatSummary(const Simulation& sim, std::uint32_t seed) {
    std::ostringstream oss;
    oss << "[summary] seed=" << seed << " mapSeed=" << sim.mapSeed() << " ticks=" << sim.tickCount()
        << " state_hash=0x" << std::hex << fnv1a64(Snapshot::serialize(sim)) << std::dec << "\n";

    int landTotal = 0;
    int armyTotal = 0;
    for (int id = 1; id <= 8; ++id) {
        const auto& f = sim.faction(id);
        int army = 0;
        for (auto e : sim.registry().view<comp::Position, comp::Collider, comp::FactionId>()) {
            if (sim.registry().get<comp::FactionId>(e).value == id &&
                !sim.registry().all_of<comp::Dead>(e) &&
                !sim.registry().all_of<comp::Projectile>(e))  // P9.3：子弹不算活兵（与 countArmies 一致）
                ++army;
        }
        landTotal += f.landCount;
        armyTotal += army;
        oss << "  " << id << " land=" << f.landCount << " city=" << f.cityCount
            << " army=" << army << " economy=" << std::fixed << std::setprecision(3)
            << f.economy << " alive=" << (f.alive ? 1 : 0) << "\n";
    }
    oss << "  total land=" << landTotal << " army=" << armyTotal << "\n";
    return oss.str();
}

int runHeadless(const CliOptions& opts) {
    const auto t0 = Clock::now();
    // 普通无头运行默认随机；CLI replay 在解析阶段显式固定为 seed 42。
    const std::uint32_t seed = opts.seedSet ? opts.seed : Rng::randomSeed();

    Simulation sim;
    if (!opts.loadPath.empty()) {
        std::string err;
        if (!Snapshot::loadFromFile(sim, opts.loadPath, &err)) {
            spdlog::error("load failed: {}", err);
            return 1;
        }
        spdlog::info("loaded snapshot '{}' (tick {})", opts.loadPath, sim.tickCount());
    } else {
        Config cfg = opts.configPath.empty() ? Config::loadFromFile(kDefaultConfigPath)
                                             : Config::loadFromFile(opts.configPath);
        // P12：--tiling 覆盖密铺；非方（且未指定 --map）→ 自动生成随机 lwmap（种子 = 主种子）。
        if (opts.tilingSet) {
            cfg.map.tiling = opts.tiling;
            if (opts.mapPath.empty()
                && cfg.map.tilingType() != TilingType::Square) {
                MapGenParams gp{cfg.map.width, cfg.map.height, 0.40, 0.08, 0.02,
                                cfg.map.cityMountainWeight, false, cfg.map.tilingType(),
                                cfg.map.forceCoastRangeMultiplier,
                                cfg.map.forceCoastStrengthMultiplier};
                const std::string mpath = MapGenerator::defaultPath(seed, gp);
                if (!MapGenerator::generate(mpath, seed, gp)) {
                    spdlog::error("tiled map generate failed");
                    return 1;
                }
                if ((gp.tiling == TilingType::Hex || gp.tiling == TilingType::Tri)
                    && (gp.height & 1))
                    --gp.height;  // 与生成器/Map::configure 一致（六/三角偶数行）
                cfg.map.width = gp.width;
                cfg.map.height = gp.height;
                cfg.map.file = mpath;
            }
        }
        if (!opts.mapPath.empty()) cfg.map.file = opts.mapPath;
        // P6 RNG 分离：主种子 seed；地图种子默认 = 主种子，可用 --map-seed 覆盖。
        sim = Simulation(cfg, seed, opts.mapSeedSet ? opts.mapSeed : seed);
        if (!sim.init()) {
            spdlog::error("simulation init failed (map missing?)");
            return 1;
        }
    }

    for (int i = 0; i < opts.ticks; ++i) sim.tick();

    if (!opts.savePath.empty()) {
        std::string err;
        if (!Snapshot::saveToFile(sim, opts.savePath, &err)) {
            spdlog::error("save failed: {}", err);
            return 1;
        }
        spdlog::info("snapshot saved to '{}' (tick {})", opts.savePath, sim.tickCount());
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0)
                             .count();
    if (opts.summary) {
        std::cout << formatSummary(sim, seed);
        std::cout.flush();
    }
    spdlog::info("elapsed {} ms for {} ticks", elapsed, opts.ticks);
    return 0;
}

}  // namespace lw
