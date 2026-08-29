// PanelManager.cpp — 可移动面板窗口绘制（开发计划 P3）。
// PanelManager 纯逻辑在 PanelManager.h（内联）；本文件只做 ImGui 窗口层：
// 统一可拖动窗口、标题栏"隐藏"按钮、位置/大小随帧回写（布局持久化）、UI 缩放适配
// （长条选项区长度随缩放倍率，统一走 ui::scaled —— 用户增注 5，2026-08 收敛）。
#include "ui/panels/PanelManager.h"

#include <imgui.h>

#include "ui/UiScale.h"

namespace lw::ui {

namespace {

// 可移动面板默认大小基准（uiScale=1 时）；默认位置放在主面板右侧。
constexpr float kBasePanelWidth = 340.0f;
constexpr float kBasePanelHeight = 240.0f;

}  // namespace

void PanelManager::draw(PanelCtx& ctx) {
    const float uiScale = ctx.controls ? ctx.controls->uiScale : 1.0f;

    // 1) 主面板固定最先绘制（左侧、不可移动），自身负责窗口（DebugPanel 实现）。
    for (const auto& p : panels_) {
        if (p->state.mainPanel) {
            if (p->state.visible) p->draw(ctx);
            break;
        }
    }

    // 2) 可移动面板：统一可拖动窗口 + 标题栏"隐藏"按钮；位置/大小随帧回写（持久化）。
    for (const auto& p : panels_) {
        if (p->state.mainPanel || !p->state.visible || !p->state.movable) continue;

        // 首次出现（未持久化位置）→ 默认放到主面板右侧；大小随 UI 缩放。
        // ImGuiCond_FirstUseEver：仅本会话首次出现时应用一次，此后拖拽/持久化位置生效。
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (p->state.x == 0.0f && p->state.y == 0.0f) {
            ImGui::SetNextWindowPos(ImVec2(display.x * 0.7f, display.y * 0.15f),
                                    ImGuiCond_FirstUseEver);
        } else {
            ImGui::SetNextWindowPos(ImVec2(p->state.x, p->state.y), ImGuiCond_FirstUseEver);
        }
        if (p->state.w <= 0.0f) p->state.w = scaled(kBasePanelWidth, uiScale);
        if (p->state.h <= 0.0f) p->state.h = scaled(kBasePanelHeight, uiScale);
        ImGui::SetNextWindowSize(ImVec2(p->state.w, p->state.h), ImGuiCond_FirstUseEver);

        // NoTitleBar → 整窗可拖动（标题栏信息自绘）；NoCollapse 防误收。
        const bool show = ImGui::Begin(p->state.id.c_str(), nullptr,
                                       ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoCollapse);
        if (show) {
            // 自绘标题栏：标题 + 右上角"隐藏"按钮。**固定在最上**——内容区用独立可滚动子窗口
            // 填满标题栏以下空间 → 无论滚轮滑到何处，标题+隐藏按钮始终显示在最上（2026-08-08）。
            ImGui::TextUnformatted(p->state.title.c_str());
            const float btnW =
                ImGui::CalcTextSize("隐藏").x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - btnW -
                            ImGui::GetStyle().WindowPadding.x - 4.0f);
            if (ImGui::Button("隐藏")) p->state.visible = false;
            ImGui::Separator();
            if (ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), false)) {
                p->draw(ctx);
            }
            ImGui::EndChild();
        }
        // 回写位置/大小（本帧点击"隐藏"后不再回写，保留隐藏前位置供再显示）。
        if (p->state.visible) {
            p->state.x = ImGui::GetWindowPos().x;
            p->state.y = ImGui::GetWindowPos().y;
            p->state.w = ImGui::GetWindowSize().x;
            p->state.h = ImGui::GetWindowSize().y;
        }
        ImGui::End();
    }
}

}  // namespace lw::ui
