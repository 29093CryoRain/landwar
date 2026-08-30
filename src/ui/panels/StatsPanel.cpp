// StatsPanel.cpp — 统计面板实现（开发计划 P11）。
// 三张表：势力×兵种（行=兵种、列=击杀/占领/产兵/人均，可按势力切换）、势力汇总、兵种汇总；
// 展示 cutoffTick（统一时刻）并高亮截止后不再变化。
#include "ui/panels/StatsPanel.h"

#include <imgui.h>

#include <cstdio>
#include <string>

#include "core/GameDefs.h"
#include "core/Simulation.h"
#include "sim/Statistics.h"
#include "ui/UiText.h"  // factionColor（势力色 → ImVec4，2026-08 统一）

namespace lw::ui {

namespace {

const char* unitName(int t) {
    switch (static_cast<ArmyType>(t)) {
        case ArmyType::normal: return "普通";
        case ArmyType::vanguard: return "先锋";
        case ArmyType::pioneer: return "开拓";
        case ArmyType::laser: return "激光";
        case ArmyType::bomb: return "爆炸";
        case ArmyType::mine: return "地雷";
        case ArmyType::pistol: return "手枪";
        case ArmyType::shotgun: return "霰弹";
        default: return "?";
    }
}

// 人均（0 除保护）。
float perCapita(int n, int produced) { return produced > 0 ? static_cast<float>(n) / produced : 0.0f; }

// 势力×兵种表（选定势力）。列：兵种/击杀/占领/产兵/人均击杀/人均占领。
void drawFactionUnitTable(const Simulation& sim, int fid) {
    const auto& st = sim.stats();
    ImGui::TextUnformatted("势力 × 兵种（");
    ImGui::SameLine(0.0f, 0.0f);
    drawFactionName(sim, fid);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted("）");
    if (ImGui::BeginTable("##stats-fu", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("兵种");
        ImGui::TableSetupColumn("击杀");
        ImGui::TableSetupColumn("占领");
        ImGui::TableSetupColumn("产兵");
        ImGui::TableSetupColumn("人均击杀");
        ImGui::TableSetupColumn("人均占领");
        ImGui::TableHeadersRow();
        for (int ut = 0; ut < kArmyTypeCount; ++ut) {
            const auto& s = st.perFactionUnit[static_cast<size_t>(fid)][static_cast<size_t>(ut)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(unitName(ut));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", s.kills);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", s.lands);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", s.produced);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f", perCapita(s.kills, s.produced));
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.2f", perCapita(s.lands, s.produced));
        }
        ImGui::EndTable();
    }
}

// 势力汇总：一行一势力（名/击杀/占领/产兵）。
void drawFactionSummary(const Simulation& sim) {
    const auto& st = sim.stats();
    ImGui::TextUnformatted("势力汇总");
    if (ImGui::BeginTable("##stats-fs", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("势力");
        ImGui::TableSetupColumn("击杀");
        ImGui::TableSetupColumn("占领");
        ImGui::TableSetupColumn("产兵");
        ImGui::TableHeadersRow();
        for (int fid : sim.selectedFactionIds()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawFactionName(sim, fid);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", st.factionKills[static_cast<size_t>(fid)]);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", st.factionLands[static_cast<size_t>(fid)]);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", st.factionProduced[static_cast<size_t>(fid)]);
        }
        ImGui::EndTable();
    }
}

// 兵种汇总：一行一兵种（名/击杀/占领/产兵）。
void drawUnitSummary(const Simulation& sim) {
    const auto& st = sim.stats();
    ImGui::TextUnformatted("兵种汇总");
    if (ImGui::BeginTable("##stats-us", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("兵种");
        ImGui::TableSetupColumn("击杀");
        ImGui::TableSetupColumn("占领");
        ImGui::TableSetupColumn("产兵");
        ImGui::TableHeadersRow();
        for (int ut = 0; ut < kArmyTypeCount; ++ut) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(unitName(ut));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", st.unitKills[static_cast<size_t>(ut)]);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", st.unitLands[static_cast<size_t>(ut)]);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", st.unitProduced[static_cast<size_t>(ut)]);
        }
        ImGui::EndTable();
    }
}

}  // namespace

StatsPanel::StatsPanel() {
    state.id = "stats";      // 唯一 id（ImGui 窗口 id / 布局持久化键）
    state.title = "统计";
    state.movable = true;    // 可移动、可隐藏（主面板开关控制）
    state.visible = false;   // 默认隐藏，用户从主面板"面板"区打开
}

void StatsPanel::draw(PanelCtx& ctx) {
    const Simulation& sim = *ctx.sim;
    const auto& st = sim.stats();
    char buf[64];
    if (st.cutoffTick > 0) {
        std::snprintf(buf, sizeof(buf), "已截止于统一（tick %llu）",
                      static_cast<unsigned long long>(st.cutoffTick));
        ImGui::TextUnformatted(buf);
    } else {
        ImGui::TextUnformatted("统计进行中…（统一后截止）");
    }
    ImGui::Separator();

    // 势力选择器（势力×兵种表按此切换）。
    static int selFaction = 1;
    if (sim.selectedFactionIds().empty()) return;
    bool stillSelected = false;
    for (int fid : sim.selectedFactionIds())
        if (fid == selFaction) stillSelected = true;
    if (!stillSelected) selFaction = sim.selectedFactionIds().front();
    ImGui::TextUnformatted("当前势力");
    ImGui::SameLine();
    drawFactionName(sim, selFaction);
    ImGui::SameLine();
    if (ImGui::BeginCombo("##faction-select", "切换")) {
        for (int fid : sim.selectedFactionIds()) {
            std::string id = "##stats-faction-" + std::to_string(fid);
            if (factionNameSelectable(sim.config().factions[static_cast<size_t>(fid)],
                                      fid == selFaction, id.c_str()))
                selFaction = fid;
        }
        ImGui::EndCombo();
    }
    drawFactionUnitTable(sim, selFaction);

    ImGui::Separator();
    drawFactionSummary(sim);

    ImGui::Separator();
    drawUnitSummary(sim);
}

}  // namespace lw::ui
