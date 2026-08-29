// DebugPanel.h — 左侧主信息面板（翻新计划 Phase 8 用户反馈合并版；开发计划 P3 改为主面板）。
// 继承 Panel 作为【主面板】：固定左侧、不可移动/隐藏（state.mainPanel=true）。
// 内容：标题、ttime/seed/实体计数、面板开关区（P3：各注册面板 checkbox）、
// 速度/暂停/UI 缩放、面板开关、玩家模式区（P2 兵种图标栏）、点选详情、
// 调试按钮、操作提示、UI 缩放。
// 只读写【渲染节奏/UI】与【调试请求】，不修改模拟状态 → 确定性/回放成立
// （重载配置是重建模拟的调试功能，标记破坏性并独立触发）。
#pragma once

#include "core/GameDefs.h"
#include "render/SpriteSheet.h"  // 兵种图标栏（P2：army.png 子图做 ImageButton）
#include "ui/panels/PanelManager.h"

namespace lw {

class Simulation;
namespace app {
struct Selection;  // 前向声明（仅引用；完整定义在 app/InputManager.h）
}

namespace ui {

class DebugPanel : public Panel {
public:
    DebugPanel();  // state：主面板（mainPanel=true、不可移动/隐藏、恒显示）

    const std::string& id() const override { return state.id; }
    void draw(PanelCtx& ctx) override;

private:
    void drawPanelToggles(PanelCtx& ctx);  // P3：各注册面板 checkbox 开关
    void drawSelection(const Simulation& sim, const app::Selection& sel);
    // 玩家模式区（P2）：兵种图标栏 + 经济 + 已选摘要 + 首都（P15 指定首都）。返回玩家势力 id（0=无）。
    static int playerFactionId(const Simulation& sim);
    void drawPlayerSection(const Simulation& sim, UiRequests& req, UiControls* controls,
                           const render::SpriteSheet& sheet);

};

}  // namespace lw::ui
}  // namespace lw
