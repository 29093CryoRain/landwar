// MessagePanel.h — 消息面板（开发计划 P4 修正，思路 6.0.5）。
// 挂到 P3 PanelManager 的可移动面板：显示重大事件消息（灭亡/统一/科技等，含事件时间前缀）。
// 消息**持续留存不自动消失**，仅超 ui.messageMaxShown 丢最旧；有消息自动弹出。
// 文案由结构化事件在 UI 层组装（MessageFormat.h）；渲染按 MessageSpan 逐段画——势力名段按
// **势力 id** 着色、字面段白色，不做字符串匹配。
// 消息存储/上限逻辑在 MessageLog.h（纯逻辑可单测）；本文件只做渲染与事件接入。
#pragma once

#include <string>
#include <vector>

#include "ui/panels/MessageLog.h"
#include "ui/panels/PanelManager.h"

namespace lw {
class Simulation;
struct GameEvent;
}

namespace lw::ui {

class MessagePanel : public Panel {
public:
    MessagePanel();
    const std::string& id() const override { return state.id; }
    void draw(PanelCtx& ctx) override;

    // Application 每帧调用：取走的 GameEvent 转为消息（按 config.ui 的 ttl/max 管理）。
    void addEvents(std::vector<lw::GameEvent> events, const Simulation& sim);
    void clear() { log_.clear(); }  // F5 重载配置时清空旧局消息

private:
    MessageLog log_;
};

}  // namespace lw::ui
