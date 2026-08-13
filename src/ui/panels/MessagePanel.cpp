// MessagePanel.cpp — 消息面板实现（开发计划 P4 修正）。
// 消息持续留存（不自动消失），仅超上限丢最旧；文本含事件时间前缀；
// 渲染时仅势力名按势力色着色，其余文本白色。
#include "ui/panels/MessagePanel.h"

#include <imgui.h>

#include <cstring>

#include "core/Simulation.h"  // Simulation::tickCount / config

namespace lw::ui {

namespace {

// 势力色（0=中立灰）。仅用于消息中的势力名。
ImVec4 factionColor(const Simulation& sim, int factionId) {
    std::array<int, 3> rgb{180, 180, 180};
    if (factionId >= 1 && factionId < static_cast<int>(sim.factions().size())) {
        rgb = sim.faction(factionId).color;
    }
    return ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f);
}

// 渲染一条消息：按势力名把文本切成 [前缀|势力名|后缀] 三段——前缀/后缀白色，
// 势力名用势力色（如 "(333s) 势力 红 被灭亡"，只有"红"红色）。factionId=0 或
// 文本中找不到势力名时整段白色（如自定义消息）。
void renderMessageText(const Simulation& sim, const Message& m) {
    const char* name = (m.factionId >= 1 && m.factionId <= kPlayerFactionCount)
                           ? factionShortName(m.factionId)
                           : nullptr;
    if (!name) {
        ImGui::TextUnformatted(m.text.c_str());
        return;
    }
    const std::string& t = m.text;
    const std::size_t pos = t.find(name);
    if (pos == std::string::npos) {
        ImGui::TextUnformatted(t.c_str());
        return;
    }
    const std::size_t nameLen = std::strlen(name);
    ImGui::TextUnformatted(t.substr(0, pos).c_str());
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, factionColor(sim, m.factionId));
    ImGui::TextUnformatted(t.substr(pos, nameLen).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(t.substr(pos + nameLen).c_str());
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
    for (auto& ev : events) {
        log_.add(std::move(ev.text), ev.tick, ev.factionId);
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
        renderMessageText(sim, m);
    }
}

}  // namespace lw::ui
