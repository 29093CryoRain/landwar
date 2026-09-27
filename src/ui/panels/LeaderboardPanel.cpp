// LeaderboardPanel.cpp — 势力榜 / 联盟榜面板实现。
#include "ui/panels/LeaderboardPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "core/Simulation.h"
#include "ui/UiText.h"
#include "world/Faction.h"

namespace lw::ui {

namespace {

// 联盟榜文本渲染规范：除势力名以外全部白色（见《兵运动与盟友系统开发文档》§5.4）。
void whiteText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

// 成员列：势力名着色，"、"白色分隔。
void drawMembers(const Simulation& sim, const std::vector<int>& members) {
    for (size_t m = 0; m < members.size(); ++m) {
        if (m > 0) {
            ImGui::SameLine(0.0f, 0.0f);
            whiteText("、");
            ImGui::SameLine(0.0f, 0.0f);
        }
        drawFactionName(sim, members[m]);
    }
}

}  // namespace

LeaderboardPanel::LeaderboardPanel(Mode mode) : mode_(mode) {
    state.movable = true;
    if (mode_ == Mode::Alliance) {
        state.id = "leaderboard_alliance";
        state.title = "排行榜(联盟)";
        state.visible = false;  // 默认隐藏，由主面板开关打开（避免与势力榜叠在同一默认位）
    } else {
        state.id = "leaderboard";
        state.title = "排行榜(势力)";
        state.visible = true;  // 保持拆分前默认可见
    }
}

void LeaderboardPanel::draw(PanelCtx& ctx) {
    if (mode_ == Mode::Alliance)
        drawAllianceBoard(ctx);
    else
        drawFactionBoard(ctx);
}

void LeaderboardPanel::drawFactionBoard(PanelCtx& ctx) {
    const Simulation& sim = *ctx.sim;
    const DebugCounts& counts = *ctx.counts;
    const double tickRate = sim.config().sim.tickRate;

    ImGui::TextUnformatted("排行榜（按领地降序）");
    ranked_.clear();
    for (int id : sim.selectedFactionIds()) ranked_.push_back(&sim.faction(id));
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

void LeaderboardPanel::drawAllianceBoard(PanelCtx& ctx) {
    const Simulation& sim = *ctx.sim;
    const DebugCounts& counts = *ctx.counts;
    const double tickRate = sim.config().sim.tickRate;

    ImGui::TextUnformatted("排行榜（按领地降序）");
    allianceRows_.clear();
    // 先收集"已入盟"的势力；本局其余势力随后作为单势力联盟参与排名。
    std::vector<int> unallied = sim.selectedFactionIds();
    for (const auto& group : sim.options().alliances) {
        AllianceRow row;
        for (int fid : group) {
            if (fid <= 0 || fid >= sim.factionCount()) continue;
            row.members.push_back(fid);
            const Faction& f = sim.faction(fid);
            row.land += f.landCount;
            row.cities += f.cityCount;
            row.armies += counts.armyPerFaction[static_cast<size_t>(fid)];
            row.economyRate += f.economyRate;
            unallied.erase(std::remove(unallied.begin(), unallied.end(), fid), unallied.end());
        }
        if (!row.members.empty()) allianceRows_.push_back(std::move(row));
    }
    for (int fid : unallied) {
        const Faction& f = sim.faction(fid);
        AllianceRow row;
        row.members.push_back(fid);
        row.land = f.landCount;
        row.cities = f.cityCount;
        row.armies = counts.armyPerFaction[static_cast<size_t>(fid)];
        row.economyRate = f.economyRate;
        allianceRows_.push_back(std::move(row));
    }
    // 名次：领地降序 → 城市降序 → 首个成员 id 升序（同值时确定，与势力榜同口径）。
    std::sort(allianceRows_.begin(), allianceRows_.end(),
              [](const AllianceRow& a, const AllianceRow& b) {
                  if (a.land != b.land) return a.land > b.land;
                  if (a.cities != b.cities) return a.cities > b.cities;
                  return a.members.front() < b.members.front();
              });

    if (ImGui::BeginTable("##alliance-leaderboard", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("名次");
        ImGui::TableSetupColumn("成员");
        ImGui::TableSetupColumn("领地");
        ImGui::TableSetupColumn("城市");
        ImGui::TableSetupColumn("兵力");
        ImGui::TableSetupColumn("经济/s");
        ImGui::TableHeadersRow();

        char buf[32];
        for (size_t i = 0; i < allianceRows_.size(); ++i) {
            const AllianceRow& row = allianceRows_[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%zu", i + 1);
            whiteText(buf);
            ImGui::TableNextColumn();
            drawMembers(sim, row.members);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", row.land);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", row.cities);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", row.armies);
            whiteText(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.4f", row.economyRate * tickRate);
            whiteText(buf);
        }
        ImGui::EndTable();
    }
}

}  // namespace lw::ui
