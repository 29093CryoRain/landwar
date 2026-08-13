// Panel.h — 面板抽象（开发计划 P3，思路 6.0）。
// 纯逻辑、可单测：面板共享控制（原 DebugPanel.h 迁入）、面板状态、绘制上下文、面板接口。
// PanelManager（注册/可见性/布局读写）见 ui/panels/PanelManager.h（内联逻辑，可脱离 UI 层单测）；
// 每帧 ImGui 窗口绘制在 ui/panels/PanelManager.cpp（app lib）。
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/GameDefs.h"

namespace lw {

class Simulation;
namespace app { struct Selection; }
namespace render { class SpriteSheet; }

namespace ui {

// 面板共享控制（Application 每帧传入，面板 UI 修改后回写）——原 DebugPanel.h 中的定义迁入。
struct UiControls {
    double speed = 1.0;
    bool paused = false;
    float uiScale = 1.0f;  // UI 缩放（font 全局缩放 + 面板宽度）
    bool capitalDesignating = false;  // P15：玩家"指定首都"模式是否开启（Application 回写显示）
};

// 面板每帧请求（Application 消费）。
struct UiRequests {
    bool reloadConfig = false;    // 重载配置（F5 等价；重建模拟，破坏当前局面）
    bool endgameSummary = false;  // 输出终局摘要到日志
    bool stepFrame = false;       // 步进一帧（F6 等价；跑一个逻辑帧后暂停）
    int selectUnitType = -1;      // 兵种图标栏点击 → 选兵种（ArmyType 值；-1=无，P2）
    bool selectNone = false;      // 图标栏最左空框点击 → 不生产（selectedType=-1，P2 修正）
    bool toggleCapitalDesignation = false;  // P15：玩家"指定首都"模式开关（无正式首都时可点）
    bool techChosen = false;      // P8：玩家本轮已做科研选择（科研弹窗设置，Application 消费）
    int  techIndex = -1;          // P8：选择的科技下标（-1 = 跳过）
};

// 每帧实体计数（Application 计算，面板展示）。
struct DebugCounts {
    std::array<int, kFactionTotal> armyPerFaction{};
    int armyTotal = 0;
    int effectCount = 0;
};

// 面板状态（可见性/可移动/位置/大小）。pos/size 用 float 而非 ImVec2 → 纯逻辑可单测、
// 布局可序列化（Options.panels 持久化）。
struct PanelState {
    std::string id;         // 唯一 id（ImGui 窗口 id 用）
    std::string title;      // 显示标题（主面板开关区 / 可移动面板标题栏）
    bool visible = false;   // 是否显示
    bool movable = false;   // 是否可拖动（主面板 = false）
    bool mainPanel = false; // 主面板：固定左侧、不可隐藏、不可移动
    float x = 0, y = 0;     // 上次位置（持久化用）
    float w = 0, h = 0;     // 上次大小（持久化用）
};

// 面板绘制上下文：Application 每帧构建，传给各面板 draw。
// 只读模拟引用 + 双向控制（面板 UI 修改后回写）。
struct PanelCtx {
    const Simulation* sim = nullptr;
    const app::Selection* selection = nullptr;
    std::uint32_t seed = 0;         // 主随机种子（首都与后续）
    std::uint32_t mapSeed = 0;      // P6：地图随机种子（地图生成 + 山/城骰子）
    const DebugCounts* counts = nullptr;
    UiControls* controls = nullptr;
    UiRequests* requests = nullptr;
    const render::SpriteSheet* sheet = nullptr;
    class PanelManager* manager = nullptr;  // 主面板列出各面板开关用
};

// 面板接口：每个面板实现 id/draw；state 由 PanelManager 统一管理（注册/可见性/布局）。
class Panel {
public:
    virtual ~Panel() = default;
    virtual const std::string& id() const = 0;
    virtual void draw(PanelCtx& ctx) = 0;
    PanelState state;
};

}  // namespace lw::ui
}  // namespace lw
