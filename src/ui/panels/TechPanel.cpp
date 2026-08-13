// TechPanel.cpp — 科技面板实现（开发计划 P8，思路 6.2 / "科技细节"）。
#include "ui/panels/TechPanel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>

#include "core/Simulation.h"
#include "ui/UiText.h"  // factionColor（势力色 → ImVec4，2026-08 统一）
#include "world/Faction.h"

namespace lw::ui {

TechPanel::TechPanel() {
    state.id = "tech";       // 唯一 id（ImGui 窗口 id / 布局持久化键）
    state.title = "科技";
    state.movable = true;    // 可移动、可隐藏（主面板开关控制）
    state.visible = false;   // 默认隐藏，用户从主面板"面板"区打开
}

void TechPanel::draw(PanelCtx& ctx) {
    const Simulation& sim = *ctx.sim;
    const auto& cfg = sim.config();
    if (cfg.tech.techs.empty()) {
        ImGui::TextUnformatted("无科技定义（config.tech 为空）");
        return;
    }
    char buf[128];
    for (int fid = 1; fid <= kPlayerFactionCount; ++fid) {
        const Faction& f = sim.faction(fid);
        std::snprintf(buf, sizeof(buf), "%s 科技等级 %d", factionShortName(fid), f.techLevel());
        ImGui::PushStyleColor(ImGuiCol_Text, factionColor(sim, fid));
        ImGui::TextUnformatted(buf);
        ImGui::PopStyleColor();
        // 科技点进度（到阈值获得科研机会）。
        if (f.tech.threshold > 0.0) {
            const double ratio = f.tech.points / f.tech.threshold;
            const float frac = ratio > 1.0 ? 1.0f : (ratio < 0.0 ? 0.0f : static_cast<float>(ratio));
            std::snprintf(buf, sizeof(buf), "科技点 %.0f/%.0f", f.tech.points, f.tech.threshold);
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), buf);
        }
        // 已拥有科技 + 等级。
        const size_t n = std::min(f.tech.levels.size(), cfg.tech.techs.size());
        bool any = false;
        for (size_t i = 0; i < n; ++i) {
            if (f.tech.levels[i] <= 0) continue;
            any = true;
            std::snprintf(buf, sizeof(buf), "  %s Lv %d", cfg.tech.techs[i].name.c_str(),
                          f.tech.levels[i]);
            ImGui::TextUnformatted(buf);
        }
        if (!any) ImGui::TextUnformatted("  （暂无科技）");
        ImGui::Separator();
    }
}

}  // namespace lw::ui
