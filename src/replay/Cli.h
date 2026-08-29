// Cli.h — 命令行参数解析（翻新计划 Phase 6）。
// 手写 -- 解析：--headless / --seed / --ticks / --speed / --config / --map / --save / --load / --summary / --replay / --screenshot。
#pragma once

#include <cstdint>
#include <string>

namespace lw {

struct CliOptions {
    bool headless = false;   // 无头模式（不创建窗口）
    bool summary = false;    // 终局按势力打印 land/city/army/经济 与 state_hash
    bool help = false;
    std::uint32_t seed = 42;  // 显式 --seed 的种子（seedSet=true 时生效）
    bool seedSet = false;     // 是否显式指定 --seed（未指定 → 用 Rng::randomSeed() 随机）
    std::uint32_t mapSeed = 0;  // P6：--map-seed 地图种子（mapSeedSet=true 时生效；默认 = 主种子）
    bool mapSeedSet = false;    // 是否显式指定 --map-seed
    int ticks = 1000;         // 步进逻辑帧数（默认烟测长度）
    double speed = 1.0;       // 倍速（窗口模式渲染节奏用；无头直接步进、忽略）
    std::string configPath;   // 空 = data/config.json
    std::string mapPath;      // 覆盖 config.map.file
    std::string savePath;     // 运行结束写存档
    std::string loadPath;     // 从存档继续（覆盖 seed/config/map）
    std::string screenshotPath;  // 窗口模式：跑到 tick=--ticks 时截图并退出（QA/视觉对拍钩子）
    bool noMenu = false;         // 窗口模式：跳过菜单直接开始（P1）
    std::string tiling;          // P12：--tiling square|hex|tri（tilingSet=true 时生效；非方自动生成 lwmap）
    bool tilingSet = false;      // 是否显式指定 --tiling
};

// 解析 argv。任何模拟控制参数（--seed/--ticks/--speed/--summary/--save/--load/--replay）都视作无头。
CliOptions parseCli(int argc, char* argv[]);

// 简短用法（--help）。
std::string cliUsage();

}  // namespace lw
