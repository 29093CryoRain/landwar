// Cli.cpp — 命令行参数解析（Phase 6）。
#include "replay/Cli.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace lw {

std::string cliUsage() {
    return "usage: landwar [--headless] [--replay [SEED]] [--seed N] [--ticks N] [--speed X]\n"
           "              [--config PATH] [--map PATH] [--save PATH] [--load PATH] [--summary]\n"
            "              [--screenshot PATH] [--map-seed N] [--tiling T]\n"
            "\n"
            "  --headless      无头运行（不创建窗口）\n"
            "  --replay [SEED] 回放：等价于 --headless --seed SEED 重跑（模拟完全确定）\n"
            "  --seed N        随机种子（默认 42；主种子，驱动首都与后续）\n"
            "  --map-seed N    地图种子（P6，默认 = 主种子；驱动随机图生成与山/城骰子）\n"
            "  --ticks N       逻辑帧数（默认 20000）\n"
            "  --speed X       倍速（无头忽略；窗口模式渲染节奏用）\n"
            "  --config PATH   config.json 路径（默认 data/config.json）\n"
            "  --map PATH      覆盖地图文件（默认取 config；--tiling 非方时自动生成 lwmap）\n"
            "  --save PATH     运行结束写存档快照\n"
            "  --load PATH     从存档继续（存档自带 config/map/rng，忽略 --seed/--config/--map）\n"
            "  --summary       终局按势力打印 land/city/army/经济 与 state_hash\n"
            "  --screenshot PATH  窗口模式：跑到 tick=--ticks 时截图保存到 PATH 并退出（QA 钩子）\n"
            "  --no-menu          窗口模式：跳过菜单直接开始（P1）\n"
            "  --tiling T         密铺类型：square | hex | tri | arch_33336 | arch_33434 |\n"
           "                      arch_3464 | arch_3636 | arch_31212 | arch_4612 | arch_488 |\n"
           "                      laves_3636 | laves_31212 | laves_4612 | laves_488 |\n"
           "                      laves_33434 | laves_33336 | laves_3464（headless 用；\n"
           "                      非方 → 自动生成随机 lwmap，种子 = 主种子）\n";
}

CliOptions parseCli(int argc, char* argv[]) {
    CliOptions o;
    bool explicitSim = false;  // 出现任何模拟控制参数 → 无头（窗口模式对它们无意义）
    const std::vector<std::string> args(argv + 1, argv + argc);

    auto takeValue = [&args](std::size_t& i, const std::string& flag) -> std::string {
        if (i + 1 >= args.size()) {
            spdlog::warn("option '{}' requires a value", flag);
            return "";
        }
        return args[++i];
    };
    auto toUint = [](const std::string& v, std::uint32_t def, const std::string& what) {
        try {
            return static_cast<std::uint32_t>(std::stoul(v));
        } catch (...) {
            spdlog::warn("bad {} '{}', using {}", what, v, def);
            return def;
        }
    };
    auto toInt = [](const std::string& v, int def, const std::string& what) {
        try {
            return std::stoi(v);
        } catch (...) {
            spdlog::warn("bad {} '{}', using {}", what, v, def);
            return def;
        }
    };
    auto toDouble = [](const std::string& v, double def, const std::string& what) {
        try {
            return std::stod(v);
        } catch (...) {
            spdlog::warn("bad {} '{}', using {}", what, v, def);
            return def;
        }
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--headless") {
            o.headless = true;
        } else if (a == "--replay") {
            o.headless = true;
            explicitSim = true;
            // 可选紧跟 seed 值（非 '-' 开头即数字）。
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                o.seed = toUint(args[++i], o.seed, "--replay seed");
                o.seedSet = true;
            }
        } else if (a == "--seed") {
            o.seed = toUint(takeValue(i, a), o.seed, "--seed");
            o.seedSet = true;
            explicitSim = true;
        } else if (a == "--map-seed") {
            // P6：地图种子（独立于主种子）。默认 = 主种子。
            o.mapSeed = toUint(takeValue(i, a), o.mapSeed, "--map-seed");
            o.mapSeedSet = true;
        } else if (a == "--ticks") {
            o.ticks = toInt(takeValue(i, a), o.ticks, "--ticks");
            explicitSim = true;
        } else if (a == "--speed") {
            o.speed = toDouble(takeValue(i, a), o.speed, "--speed");
            explicitSim = true;
        } else if (a == "--config") {
            o.configPath = takeValue(i, a);
        } else if (a == "--map") {
            o.mapPath = takeValue(i, a);
            explicitSim = true;
        } else if (a == "--save") {
            o.savePath = takeValue(i, a);
            explicitSim = true;
        } else if (a == "--load") {
            o.loadPath = takeValue(i, a);
            explicitSim = true;
        } else if (a == "--summary") {
            o.summary = true;
            explicitSim = true;
        } else if (a == "--screenshot") {
            // 窗口模式 QA 钩子：不强制无头（需要窗口/渲染器截图）。
            o.screenshotPath = takeValue(i, a);
        } else if (a == "--no-menu") {
            // 窗口模式：跳过菜单直接开始（P1）。
            o.noMenu = true;
        } else if (a == "--tiling") {
            // P12：密铺类型（square/hex/tri）——headless 确定性测试用；窗口走菜单下拉。
            o.tiling = takeValue(i, a);
            o.tilingSet = true;
        } else if (a == "-h" || a == "--help") {
            o.help = true;
        } else {
            spdlog::warn("unknown option '{}' ignored", a);
        }
    }
    if (explicitSim) o.headless = true;
    // --screenshot 是窗口模式 QA 钩子：截图需要窗口/渲染器，强制窗口模式（覆盖模拟参数隐含的无头）。
    if (!o.screenshotPath.empty()) o.headless = false;
    return o;
}

}  // namespace lw
