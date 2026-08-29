// LeaderboardPanel.cpp — 势力排行榜面板实现。
#include "ui/panels/LeaderboardPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "core/Simulation.h"
#include "ui/UiText.h"
#include "world/Faction.h"

namespace lw::ui {

LeaderboardPanel::LeaderboardPanel() {
    state.id = "leaderboard";
    state.title = "排行榜";
    state.movable = true;
    state.visible = true;  // 保持拆分前默认可见，之后可由面板开关隐藏
}

void LeaderboardPanel::draw(PanelCtx& ctx) {
    const Simulation& sim = *ctx.sim;
    const DebugCounts& counts = *ctx.counts;
    const double tickRate = sim.config().sim.tickRate;

    ImGui::TextUnformatted("排行榜（按领地降序）");
    ranked_.clear();
    for (int id = 1; id <= kPlayerFactionCount; ++id) ranked_.push_back(&sim.faction(id));
    std::sort(ranked_.begin(), ranked_.end(),
              [](const Faction* a, const Faction* b) { return a->landCount > b->landCount; });

    if (ImGui::BeginTable("##leaderboard", 9,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("名次");
        ImGui::TableSetupColumn("势力");
        ImGui::TableSetupColumn("领地");
        ImGui::TableSetupColumn("城市");
        ImGui::TableSetupColumn("兵力");
        ImGui::TableSetupColumn("科技");
        ImGui::TableSetupColumn("最高城");
        ImGui::TableSetupColumn("经济/s");
        ImGui::TableSetupColumn("科技/s");
        ImGui::TableHeadersRow();

        char buf[32];
        for (int i = 0; i < kPlayerFactionCount; ++i) {
            const Faction* f = ranked_[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", i + 1);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, factionColor(sim, f->id));
            ImGui::TextUnformatted(factionShortName(f->id));
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", f->landCount);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", f->cityCount);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", counts.armyPerFaction[static_cast<size_t>(f->id)]);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", f->techLevel());
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.2f", f->maxCityLevel);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.4f", f->economyRate * tickRate);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.4f", f->techRate * tickRate);
            ImGui::TextUnformatted(buf);
        }
        ImGui::EndTable();
    }
}

}  // namespace lw::ui
