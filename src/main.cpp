// 程序入口：无头 CLI 或 SDL 窗口模式（翻新计划 §3.1，Phase 6/7；开发计划 P1 菜单）。
//   headless → runHeadless（--summary 确定性回归基线，见 Phase 9）；
//   窗口模式 → Application：默认进菜单（点「开始游戏」按 Options 构建模拟）；
//   跳过菜单（--load / --screenshot / --no-menu / 有模拟控制参数）→ 直接开始。
#define SDL_MAIN_HANDLED  // 自行处理 main()，不链接 SDL2main，保持控制台输出可见
#include <iostream>
#include <string>

#include <spdlog/spdlog.h>

#include "app/Application.h"
#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Simulation.h"
#include "replay/Cli.h"
#include "replay/Headless.h"
#include "replay/Snapshot.h"

int main(int argc, char* argv[]) {
    lw::initLog();
    const lw::CliOptions opts = lw::parseCli(argc, argv);
    if (opts.help) {
        std::cout << lw::cliUsage();
        return 0;
    }
    if (opts.validateConfig) {
        const std::string configPath = opts.configPath.empty()
                                           ? std::string(lw::kDefaultConfigPath)
                                           : opts.configPath;
        std::string error;
        if (!lw::Config::validateFile(configPath, &error)) {
            spdlog::error("config validation failed: {}", error);
            return 1;
        }
        std::cout << "config valid: " << configPath << '\n';
        return 0;
    }
    if (opts.headless) return lw::runHeadless(opts);

    // 默认种子随机（每次启动不同）；显式 --seed 走确定性路径。
    const std::uint32_t seed = opts.seedSet ? opts.seed : lw::Rng::randomSeed();
    const std::string configPath =
        opts.configPath.empty() ? std::string(lw::kDefaultConfigPath) : opts.configPath;
    const std::string optionsPath = lw::kDefaultOptionsPath;  // 运行期产物 → userdata/（2026-08）

    // 窗口模式需要 userdata/（options 保存、随机图生成、截图）。失败不阻断（读档/只读场景兜底）。
    lw::ensureUserDataDirs();

    // 跳过菜单：--load（读档直接开始）、--screenshot（QA 截图）、--no-menu（显式跳过）。
    const bool skipMenu = opts.noMenu || !opts.loadPath.empty() || !opts.screenshotPath.empty();
    lw::Application app(seed, configPath, optionsPath, skipMenu);
    if (opts.mapSeedSet) app.setCliMapSeed(opts.mapSeed);  // P6：地图种子 CLI 覆盖
    if (!opts.mapPath.empty()) app.setMapOverride(opts.mapPath);
    if (!opts.loadPath.empty()) {
        lw::Simulation sim;
        std::string err;
        if (!lw::Snapshot::loadFromFile(sim, opts.loadPath, &err)) {
            spdlog::error("load failed: {}", err);
            return 1;
        }
        spdlog::info("loaded snapshot '{}' (tick {})", opts.loadPath, sim.tickCount());
        app.setPrebuilt(std::move(sim));
    }
    app.setSpeed(opts.speed);  // --speed 控制渲染节奏（QA 截图可加速到目标 tick）
    if (!opts.screenshotPath.empty()) app.setScreenshot(opts.screenshotPath, opts.ticks);
    return app.run();
}
