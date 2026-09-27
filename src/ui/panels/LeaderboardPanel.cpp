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
    for (int id : sim.selectedFactionIds()) ranked_.push_back(&sim.faction(id));
    std::sort(ranked_.begin(), ranked_.end(),
              [](const Faction* a, const Faction* b) { return a->landCount > b->landCount; });

    // 联盟行文本渲染规范：除势力名以外全部白色（见开发文档 §5.4）。
    const auto whiteText = [](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    };

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
        // ---- 联盟行（置于势力行之前；能聚合的列按成员逐项求和）----
        const auto& alliances = sim.options().alliances;
        for (size_t g = 0; g < alliances.size(); ++g) {
            const auto& group = alliances[g];
            if (group.empty()) continue;
            int land = 0, cities = 0, armies = 0, tech = 0;
            double maxCity = 0.0, economy = 0.0, techRate = 0.0;
            for (int fid : group) {
                if (fid <= 0 || fid >= sim.factionCount()) continue;
                const Faction& f = sim.faction(fid);
                land += f.landCount;
                cities += f.cityCount;
                armies += counts.armyPerFaction[static_cast<size_t>(fid)];
                tech += f.techLevel();
                maxCity = std::max(maxCity, f.maxCityLevel);
                economy += f.economyRate * tickRate;
                techRate += f.techRate * tickRate;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "联盟 %zu", g + 1);
            whiteText(buf);
            ImGui::TableNextColumn();
            whiteText("成员：");
            for (size_t m = 0; m < group.size(); ++m) {
                const int fid = group[m];
                if (fid <= 0 || fid >= sim.factionCount()) continue;
                ImGui::SameLine(0.0f, 0.0f);
                drawFactionName(sim, fid);  // 势力名按势力色
                if (m + 1 < group.size()) {
                    ImGui::SameLine(0.0f, 0.0f);
                    whiteText("、");
                }
            }
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", land);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", cities);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", armies);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", tech);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.2f", maxCity);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.4f", economy);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.4f", techRate);
            whiteText(buf);
        }
        if (!alliances.empty()) {
            // 联盟与势力之间的视觉分隔行（不参与排序/聚合）。
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            whiteText("—— 势力 ——");
        }

        for (int i = 0; i < static_cast<int>(ranked_.size()); ++i) {
            const Faction* f = ranked_[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", i + 1);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            drawFactionName(sim, f->id);
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
