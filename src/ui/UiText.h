// UiText.h — UI 文本渲染统一辅助（2026-08 工程改进）。
// 历史：势力色→ImVec4 转换在各面板 5 份副本（DebugPanel/Menu/TechPanel/StatsPanel/
// MessagePanel 各写一份）；彩色文本（消息"前缀白+势力名着色+后缀白"、科技旧值黄/新值绿）
// 各写各的。本文件收敛：
//   - rgbToImVec4 / factionColor：颜色转换单点
//   - TextSeg + drawSegmentedText：分段彩色文本（单行；段间无缝拼接）
//   - drawTextWithHighlight：文本中定位 token（如势力名）并着色，其余用 base 色
// 纯展示层（依赖 ImGui），编入应用目标；不可单测（无 ImGui 上下文）。
#pragma once

#include <array>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/Simulation.h"

namespace lw::ui {

// array<int,3>（势力色等 0-255）→ ImVec4。
inline ImVec4 rgbToImVec4(const std::array<int, 3>& rgb, float a = 1.0f) {
    return ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, a);
}

// 势力色（0=中立/越界 → 灰 180）。消息/科技/统计/排行榜共用。
inline ImVec4 factionColor(const Simulation& sim, int factionId) {
    std::array<int, 3> rgb{180, 180, 180};
    if (factionId >= 1 && factionId < static_cast<int>(sim.factions().size()))
        rgb = sim.faction(factionId).color;
    return rgbToImVec4(rgb);
}

// 一段彩色文本（纯单段 → 等价 TextUnformatted；空段自动跳过）。
struct TextSeg {
    std::string text;
    ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

// 绘制分段彩色文本：单行，段间以 SameLine(0,0) 无缝拼接，颜色按段 Push/Pop。
void drawSegmentedText(const std::vector<TextSeg>& segs);

// 便捷：在 text 中定位 token（如势力名）并高亮（highlight 色），其余 base 色；
// 找不到 token → 整段 base 色。MessagePanel 势力名着色即此。
void drawTextWithHighlight(const std::string& text, const std::string& token,
                           const ImVec4& highlight, const ImVec4& base = ImVec4(1, 1, 1, 1));

}  // namespace lw::ui
