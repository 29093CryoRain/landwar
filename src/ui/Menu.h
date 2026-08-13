// Menu.h — 主菜单（开发计划 P1 任务 6，思路 1）+ 二级「选择地图」界面（P6）。
// 启动时在窗口内绘制；点「开始游戏」保存 options.json 并进入游戏。
// 菜单阶段 Simulation 尚未构建，势力颜色/名字取自 Config（uiConfig_）。
// P6：主屏加「选择地图…」→ 二级选图屏（预装图带预览、随机图填参、地图种子编辑）。
// 预览纹理缓存由菜单持有（析构释放，须先于 SDL_DestroyRenderer）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "core/Config.h"
#include "core/Options.h"
#include "world/Map.h"

namespace lw::ui {

struct MenuResult {
    bool started = false;  // 点「开始游戏」
    bool quit = false;     // 点「退出」
};

// 预览纹理缓存（P6）：按 key 缓存 MapPreview 生成的缩略图。菜单持有；析构释放。
struct PreviewCache {
    struct Entry {
        std::string key;
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;  // 纹理像素尺寸（显示用）
    };
    std::vector<Entry> items;
    ~PreviewCache() { release(); }

    void release();  // 释放全部纹理（可重复调用）
    void clear();    // 清空缓存（地图种子/参数变化时失效重生成）
    // 命中缓存返回 tex；未命中用 renderMapPreview 渲染并缓存。渲染失败返回 nullptr（已记错）。
    SDL_Texture* get(SDL_Renderer* ren, const std::string& key, const Map& map, int previewW,
                     int* outW = nullptr, int* outH = nullptr);
    // 只查询缓存（不渲染）；未生成/已失效返回 nullptr。
    SDL_Texture* lookup(const std::string& key, int* outW = nullptr, int* outH = nullptr);
};

// 菜单跨帧状态（Application::runMenu 内局部持有；跨帧保持，二级屏/草稿/预览缓存）。
struct MenuState {
    enum class Screen { Main, MapSelect } screen = Screen::Main;
    bool seededOnce = false;      // 选图页首开已完成初始化（含地图种子自动随机）
    std::uint32_t mainSeed = 0;   // 主种子（只读展示）
    std::uint32_t draftSeed = 0;  // 编辑中的地图种子（预览/随机图/游戏共用，一套）
    bool randomMode = false;      // 随机 vs 预装
    std::string draftFile;        // 预装模式下选中的文件
    int randW = 105, randH = 95;  // 随机图长宽
    float seaRatio = 0.40f;       // 随机图海占比（内部；UI 显示为"陆地占比"=1-海占比）
    float mtnDensity = 0.08f;     // 随机图山密度（由 mtnT 派生）
    float cityDensity = 0.02f;    // 随机图城密度（由 cityT 派生）
    // 山/城密度滑条位置（对数域 [0,1]，跨帧稳定——ImGui 直改它，避免每帧从密度重算导致手感黏滞）。
    float mtnT = 0.5f, cityT = 0.5f;
    bool forceCoast = false;      // 强制边缘为海（随机图）
    bool wrap = false;            // 地图边界贯通（环绕，P10；预装/随机图通用）
    std::string genPath;          // 最近一次随机图生成的 BMP 路径（预览/开始复用）
    std::string genError;         // 随机图生成/加载失败信息（空 = 成功）
    PreviewCache previews;
};

// 枚举 data 目录下 *.bmp（排序），返回相对路径（如 data/map_bigIslands.bmp）。
// 生成物 gen_*.bmp 会混入（菜单选图页过滤），保留原接口语义。
std::vector<std::string> enumerateMapFiles(const std::string& dataDir);

// 每帧绘制菜单（在 beginImGuiFrame / renderImGui 之间调用一次）。ren 供预览纹理生成。
// cfg 提供势力颜色/名字与地形参数（菜单阶段 sim 未构建）；无效选项（多个玩家）会禁用「开始游戏」。
// uiScale：菜单内 UI 缩放滑条读写（Application 持有；游戏面板共用同一缩放，见 DebugPanel）。
MenuResult drawMenu(MenuState& st, SDL_Renderer* ren, Options& options, const Config& cfg,
                    const std::vector<std::string>& mapFiles, float& uiScale);

}  // namespace lw::ui
