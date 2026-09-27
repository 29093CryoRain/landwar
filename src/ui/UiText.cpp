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

void drawTextWithFactionNames(const Simulation& sim, const std::vector<int>& factionIds,
                              const std::string& text, const ImVec4& base) {
    std::vector<TextSeg> segments;
    const auto appendBase = [&](std::size_t begin, std::size_t end) {
        if (end > begin) segments.push_back({text.substr(begin, end - begin), base});
    };
    // 从左到右扫描：当前位置能匹配某个待着色势力名 → 输出该名的逐字颜色并跳过；
    // 否则输出一个 UTF-8 字符的 base 段。同位置多个名字可匹配时取最长（防止短名截断长名）。
    std::size_t plainBegin = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        int matched = 0;
        std::size_t matchedLen = 0;
        for (int fid : factionIds) {
            if (fid < 1 || fid >= sim.factionCount()) continue;
            const std::string& name = sim.faction(fid).name;
            if (name.empty() || name.size() <= matchedLen) continue;
            if (text.compare(pos, name.size(), name) == 0) {
                matched = fid;
                matchedLen = name.size();
            }
        }
        if (matched == 0) {
            pos += utf8CharLength(text, pos);
            continue;
        }
        appendBase(plainBegin, pos);
        std::vector<TextSeg> nameSegments =
            factionNameSegments(sim.config().factions[static_cast<std::size_t>(matched)]);
        segments.insert(segments.end(), nameSegments.begin(), nameSegments.end());
        pos += matchedLen;
        plainBegin = pos;
    }
    appendBase(plainBegin, text.size());
    drawSegmentedText(segments);
}

void drawTextWithFactionName(const Simulation& sim, int factionId, const std::string& text,
                             const ImVec4& base) {
    drawTextWithFactionNames(sim, {factionId}, text, base);
}

}  // namespace lw::ui
