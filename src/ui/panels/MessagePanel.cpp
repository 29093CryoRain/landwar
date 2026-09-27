// MessagePanel.cpp — 消息面板实现（开发计划 P4 修正）。
// 消息持续留存（不自动消失），仅超上限丢最旧。
// 文案由结构化事件在 UI 层组装（MessageFormat.h）；渲染按 MessageSpan 逐段绘制：
// 势力名段 → 按势力色逐字着色，字面段 → 白色。**不做字符串匹配**（旧 text.find(名字) 已删除）。
#include "ui/panels/MessagePanel.h"

#include <imgui.h>

#include <vector>

#include "core/Simulation.h"  // Simulation::config
#include "ui/UiText.h"
#include "ui/panels/MessageFormat.h"

namespace lw::ui {

namespace {

// 逐段绘制：势力名段走 drawFactionName(sim, id)（内部逐字着色），字面段白色；
// 段间 SameLine(0,0) 无缝拼接（与 drawSegmentedText 同款）。
void drawMessageSpans(const Simulation& sim, const std::vector<MessageSpan>& spans) {
    bool first = true;
    for (const auto& span : spans) {
        if (span.factionId >= 0) {
            if (!first) ImGui::SameLine(0.0f, 0.0f);
            drawFactionName(sim, span.factionId);
            first = false;
        } else if (!span.literal.empty()) {
            if (!first) ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::TextUnformatted(span.literal.c_str());
            ImGui::PopStyleColor();
            first = false;
        }
    }
    if (first) ImGui::TextUnformatted("");  // 空消息（防御）：仍占一行
}

}  // namespace

MessagePanel::MessagePanel() {
    state.id = "message";          // 唯一 id（ImGui 窗口 id / 布局持久化键）
    state.title = "消息";
    state.movable = true;          // 可移动、可隐藏
    state.visible = false;         // 默认隐藏；有消息自动弹出（addEvents）
}

void MessagePanel::addEvents(std::vector<lw::GameEvent> events, const Simulation& sim) {
    if (events.empty()) return;
    const int maxShown = sim.config().ui.messageMaxShown;
    for (const auto& ev : events) {
        log_.add(formatGameEvent(sim, ev), ev.tick);
    }
    log_.prune(maxShown);  // 消息过多 → 立即丢最旧（不过期）
    // 2026-08-08 用户定夺：消息面板被隐藏后**不再自动弹出**（有消息也继续隐藏；
    // 由用户从主面板"面板"区重新打开）。消息仍持续留存，打开即可见。
}

void MessagePanel::draw(PanelCtx& ctx) {
    const Simulation& sim = *ctx.sim;
    log_.prune(sim.config().ui.messageMaxShown);  // 显示前按上限丢最旧

    if (log_.empty()) {
        ImGui::TextUnformatted("（暂无消息）");
        return;
    }
    for (const auto& m : log_.messages()) {
        drawMessageSpans(sim, m.spans);
    }
}

}  // namespace lw::ui
