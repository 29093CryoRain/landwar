// Application.h — SDL 窗口应用（翻新计划 Phase 7/8，§3.1 app/；开发计划 P1 菜单阶段）。
// 职责：SDL 窗口/渲染器初始化（复用 scaffold 的 SDL/IMG/ImGui 序列，含软渲染兜底）、
// 固定步长主循环（§3.7：模拟 1/60s/tick，渲染独立帧率）、Phase 8 交互
// （InputManager 事件→动作、Camera 缩放/平移、点选、DebugPanel、F5 重载配置、F12 截图）、
// P1 菜单阶段（skipMenu=false 时先跑菜单，点「开始游戏」后按 Options 构建 Simulation）。
// 交互只改渲染节奏与视图，绝不触碰 sim RNG/状态 → 确定性回放始终成立。
// 退出：窗口关闭按钮（SDL_QUIT）。ESC 双击=暂停（原版语义，单击不动作）。
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "app/InputManager.h"
#include "core/GameDefs.h"
#include "core/MathUtil.h"
#include "core/Options.h"
#include "core/Paths.h"
#include "core/Simulation.h"
#include "render/ArmyRenderer.h"
#include "render/Camera.h"
#include "render/CityMarkerRenderer.h"
#include "render/CityRenderer.h"
#include "render/EffectRenderer.h"
#include "render/MapRenderer.h"
#include "render/ProjectileRenderer.h"
#include "render/SpriteSheet.h"
#include "ui/DebugPanel.h"             // 主面板（P3：固定左侧、不可移动/隐藏）
#include "ui/panels/MessagePanel.h"     // 消息面板占位壳（P4 填充真实内容）
#include "ui/panels/PanelManager.h"     // 多面板管理器（P3）

namespace lw {

class Application {
public:
    // seed/configPath/optionsPath 供菜单构建与 F5 重载用；skipMenu=true 跳过菜单直接开始
    // （--load/--screenshot/--no-menu）。模拟在 run() 中构建（菜单后）或经 setPrebuilt 注入。
    Application(std::uint32_t seed, std::string configPath, std::string optionsPath, bool skipMenu);
    ~Application();

    // 不可拷贝（持有 SDL 资源与唯一模拟）。
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();  // 返回进程退出码（0 = 正常退出）。

    // P1：注入已构建的 Simulation（--load 读档）；随后 run() 直接进游戏（跳过菜单）。
    void setPrebuilt(Simulation sim);
    // P1：CLI --map 覆盖（菜单地图选择的预填/直接开始时的地图覆盖）。
    void setMapOverride(std::string path) { mapOverride_ = std::move(path); }
    // P6：CLI --map-seed 覆盖（地图种子；否则用 options_.map.randomSeed，菜单字段权威）。
    void setCliMapSeed(std::uint32_t s) { cliMapSeed_ = s; }

    // 时间控制（main.cpp 从 --speed 设置；运行中由 InputManager/DebugPanel 接管）。
    void setSpeed(double speed) { speed_ = speed; }
    void setPaused(bool paused) { paused_ = paused; }

    // QA 钩子：跑到 tick==targetTick 时把当前帧存到 path 并退出（视觉对拍/自动截图用）。
    void setScreenshot(const std::string& path, int targetTick) {
        screenshotPath_ = path;
        screenshotTick_ = targetTick;
    }

private:
    bool init();              // SDL/渲染器/精灵表/ImGui/相机/渲染器组（尺寸来自 uiConfig_）
    void processEvents();     // SDL 事件 → InputManager + ImGui 透传 + 退出
    void applyInputs();       // 本帧输入意图 → 相机/倍速/暂停/点选/截图/重载
    void renderFrame();       // 清黑 → 地图 → 兵 → 特效 → 指示圈 → ImGui 面板 → Present
    void computeCounts();     // 每帧统计兵/每势力兵数/特效数（DebugPanel 展示）
    void pickSelection();     // 左键单击：点选兵/城市/陆地格
    int playerFactionId() const;      // 玩家操控势力 id（Faction.aiId==1）；0=无（P2）
    void updatePlayerIntent();        // 玩家意图：兵种键/城市点击/悬停城/校验（P2）
    void initPlayerIntent();          // 有玩家：初始意图（普通兵+首都）+ 进入即暂停（P2 修正）
    void reloadConfig();      // F5/面板按钮：读盘重建模拟（同种子重启，调试）
    void reloadPalette();     // F7：按当前 uiConfig_ 重烘焙全部势力色（不重建模拟，⑫）
    bool applyPaletteColors();  // 按 uiConfig_ 重烘焙 兵表/山/城/标记/特效 颜色（init/F5/F7 共用）
    void endgameSummary();    // 输出终局摘要到日志（headless --summary 同款）
    void takeScreenshot();    // F12：自动命名 userdata/screenshots/screenshot_N.png
    void saveScreenshot(const std::string& path);  // RenderReadPixels + IMG_SavePNG
    static double cycleSpeed(double cur, int dir);  // 在 1/2/4/8 档间步进

    // P3 面板系统：
    void setupPanels();       // 注册主面板 + 可移动面板，恢复 options.panels 布局
    void syncPanelLayout();   // 把当前面板布局写回 options_.panels（退出持久化）
    // P4 消息：每帧取走 sim 事件 → 消息面板（drain）。
    void drainEvents();
    // P8 玩家科研三选一弹窗：玩家势力 researchPending 时绘制（点选/跳过写回 req），
    // 由 renderFrame 强制暂停并在选后恢复。
    void drawTechResearchModal(ui::UiRequests& req);

    // P1 菜单阶段：
    bool runMenu();           // 菜单循环；返回是否点「开始游戏」
    bool ensureSimulation();  // 确保 sim_ 就绪（prebuilt 或从 options 构建）
    bool buildSimulation();   // 按 options_/mapOverride_/configPath_ 构建 Simulation
    // 应用当前地图选择到 cfg（--map 覆盖 > 菜单 options 选择 > 随机图生成 > config 默认）。
    // buildSimulation 与 reloadConfig 共用 → F5 重载不会把地图重置回 config.json 默认（P5 改版修复）。
    // P6：Kind::Random 时生成随机图 BMP 并设置 cfg.map.width/height/file（确定性：seed+参数）。
    void resolveMapSelection(Config& cfg);
    // P6：地图尺寸可能与 config 默认不同（随机图长宽可调）→ 重配屏幕变换 tf_ 与相机
    // （否则相机 mapW/mapH、tf_.mapHeight 按旧尺寸算 → 大图放大后平移够不到边缘，2026-08-06）。
    void syncCameraToMap();
    // P6：当前生效的地图种子（CLI --map-seed 覆盖，否则 = 菜单 options 字段）。
    std::uint32_t effectiveMapSeed() const { return cliMapSeed_ ? *cliMapSeed_ : options_.map.randomSeed; }
    // （uiConfig_ 声明见成员区——必须在 configPath_/options_ 之后，避免初始化顺序读取未初始化成员）

    Simulation sim_;
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    bool vsync_ = false;  // 渲染器 vsync 是否生效（生效则 Present 自限帧率，不再 SDL_Delay）
    bool imguiReady_ = false;
    math::ScreenTransform tf_;
    render::Camera camera_;
    render::SpriteSheet sheet_;
    std::unique_ptr<render::MapRenderer> mapRenderer_;
    std::unique_ptr<render::CityRenderer> cityRenderer_;  // P13：基建格贴图 + 等级图标 + 细线围区
    std::unique_ptr<render::ArmyRenderer> armyRenderer_;
    std::unique_ptr<render::EffectRenderer> effectRenderer_;
    std::unique_ptr<render::ProjectileRenderer> projectileRenderer_;  // 子弹（P9）
    std::unique_ptr<render::CityMarkerRenderer> cityMarker_;  // 玩家产兵城/悬停指示圈（P2）
    // P2 悬停/选中城 id（= Map.cities() 下标；-1 = 无）。2026-08-07：由坐标改为 id，
    // 渲染时经 map 解析成 CityTarget（中心 + w/h）→ 指示环/箭头可随城市基建地块尺寸缩放。
    int hoverCityId_ = -1;   // 悬停城（四箭头指示）
    int selCityId_ = -1;     // 选中产兵城（旋转环指示；与 PlayerIntent 中心坐标同步）
    bool capitalDesignationMode_ = false;  // P15：玩家"指定首都"模式（点击己方城指定候补新都）
    ui::PanelManager panels_;    // 面板管理器（P3：主面板 + 可移动面板）
    ui::MessagePanel* messagePanel_ = nullptr;  // 消息面板（P4；PanelManager 持有，仅存指针）

    std::uint32_t seed_ = 42;          // 模拟种子（主种子，F5 重载用）
    std::optional<std::uint32_t> cliMapSeed_;  // P6：CLI --map-seed 覆盖（地图种子）
    std::string configPath_ = kDefaultConfigPath;  // F5 重载用
    std::string optionsPath_ = kDefaultOptionsPath;  // 菜单选项路径（开始游戏时保存；userdata/）
    std::string mapOverride_;          // CLI --map 覆盖
    Options options_;                  // 菜单选项（启动时从 optionsPath_ 读取）
    std::array<std::array<int, 3>, kFactionTotal> tileColors_{};  // 地块填色（config.factionTileColor 缓存）
    Config uiConfig_;                  // init() 的窗口/相机尺寸来源（菜单阶段 sim 未构建）。
                                       // 声明在 configPath_/optionsPath_/options_ 之后 →
                                       // 初始化顺序保证 ctor 中读取它们时已就绪。
    bool skipMenu_ = false;            // 跳过菜单（--load/--screenshot/--no-menu）
    bool menuStarted_ = false;         // 菜单已点「开始游戏」
    bool prebuilt_ = false;            // 经 setPrebuilt 注入（--load）
    bool simReady_ = false;            // sim_ 已构建/注入
    std::vector<std::string> mapFiles_;  // data/ 下地图文件列表（菜单）
    app::InputManager input_;
    app::Selection selection_;
    ui::DebugCounts counts_;

    double speed_ = 1.0;   // 倍速（§3.7：只影响渲染节奏；档位 0.25x~8x）
    bool paused_ = false;  // 暂停（不 tick）
    bool stepPending_ = false;  // 步进（F6/面板按钮）：暂停状态下推进一帧，见 run()
    float uiScale_ = 2.0f; // UI 缩放（FontGlobalScale + 面板宽度；默认 2.0，2026-08-03 用户定夺）
    bool quit_ = false;
    int screenshotCounter_ = 0;
    std::string screenshotPath_;  // 非空 = 截图 QA 钩子激活
    int screenshotTick_ = -1;     // 截图目标 tick
    bool screenshotTaken_ = false;
};

}  // namespace lw
