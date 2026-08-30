// UiText.cpp — 分段彩色文本绘制实现（2026-08 工程改进，见 UiText.h 说明）。
#include "ui/UiText.h"

#include <cstddef>

namespace lw::ui {

namespace {

size_t utf8CharLength(const std::string& text, size_t offset) {
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    size_t length = lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
    if (offset + length > text.size()) length = 1;
    return length;
}

std::vector<TextSeg> factionNameSegments(const Config::Faction& faction) {
    std::vector<TextSeg> result;
    for (size_t pos = 0, index = 0; pos < faction.name.size(); ++index) {
        const size_t length = utf8CharLength(faction.name, pos);
        const std::string source = index < faction.nameColors.size()
                                       ? faction.nameColors[index]
                                       : "primary";
        const auto& color = source == "secondary" ? faction.secondary : faction.color;
        result.push_back({faction.name.substr(pos, length), rgbToImVec4(color)});
        pos += length;
    }
    return result;
}

}  // namespace

void drawSegmentedText(const std::vector<TextSeg>& segs) {
    bool first = true;
    for (const auto& seg : segs) {
        if (seg.text.empty()) continue;
        if (!first) ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, seg.color);
        ImGui::TextUnformatted(seg.text.c_str());
        ImGui::PopStyleColor();
        first = false;
    }
}

void drawTextWithHighlight(const std::string& text, const std::string& token,
                           const ImVec4& highlight, const ImVec4& base) {
    const std::size_t pos = token.empty() ? std::string::npos : text.find(token);
    if (pos == std::string::npos) {
        drawSegmentedText({{text, base}});
        return;
    }
    std::vector<TextSeg> segs;
    if (pos > 0) segs.push_back({text.substr(0, pos), base});
    segs.push_back({text.substr(pos, token.size()), highlight});
    if (pos + token.size() < text.size())
        segs.push_back({text.substr(pos + token.size()), base});
    drawSegmentedText(segs);
}

void drawFactionName(const Config::Faction& faction) {
    if (faction.name.empty()) {
        ImGui::TextUnformatted("?");
        return;
    }
    drawSegmentedText(factionNameSegments(faction));
}

void drawFactionName(const Simulation& sim, int factionId) {
    if (factionId < 0 || factionId >= sim.factionCount()) {
        ImGui::TextUnformatted("?");
        return;
    }
    drawFactionName(sim.config().factions[static_cast<size_t>(factionId)]);
}

bool factionNameSelectable(const Config::Faction& faction, bool selected, const char* id) {
    const ImVec2 textPos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetTextLineHeight();
    if (selected) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            textPos, ImVec2(textPos.x + width, textPos.y + height),
            ImGui::GetColorU32(ImGuiCol_Header));
    }
    drawFactionName(faction);
    ImGui::SetCursorScreenPos(textPos);
    return ImGui::InvisibleButton(id, ImVec2(width, height));
}

void drawTextWithFactionName(const Simulation& sim, int factionId, const std::string& text,
                             const ImVec4& base) {
    if (factionId < 0 || factionId >= sim.factionCount()) {
        drawSegmentedText({{text, base}});
        return;
    }
    const std::string& name = sim.faction(factionId).name;
    const size_t pos = name.empty() ? std::string::npos : text.find(name);
    if (pos == std::string::npos) {
        drawSegmentedText({{text, base}});
        return;
    }
    std::vector<TextSeg> segments;
    if (pos > 0) segments.push_back({text.substr(0, pos), base});
    std::vector<TextSeg> nameSegments = factionNameSegments(sim.config().factions[
        static_cast<size_t>(factionId)]);
    segments.insert(segments.end(), nameSegments.begin(), nameSegments.end());
    if (pos + name.size() < text.size()) segments.push_back({text.substr(pos + name.size()), base});
    drawSegmentedText(segments);
}

}  // namespace lw::ui
