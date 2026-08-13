// UiText.cpp — 分段彩色文本绘制实现（2026-08 工程改进，见 UiText.h 说明）。
#include "ui/UiText.h"

namespace lw::ui {

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

}  // namespace lw::ui
