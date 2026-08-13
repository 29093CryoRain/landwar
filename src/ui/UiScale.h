// UiScale.h — UI 缩放统一辅助（2026-08 工程改进）。
// 历史：各面板/菜单各自手写 `宽度 × uiScale`（README"长条选项区长度随缩放"打了多处补丁），
// 规则散落、新面板容易漏。本文件收敛共享常量与公式：
//   - 缩放范围 kMinUiScale/kMaxUiScale（滑条/面板共用；Application uiScale_ 默认 2.0）
//   - 主面板宽度 mainPanelWidth()（uiScale=1 基准 290 → 2.0×=580 ≤ 地图 panelWidth 600）
//   - 通用控件基准宽度（滑条 220 / 按钮 140 / 下拉 120）
// 面板特有基准（如菜单 620、可移动面板 340）保留在各文件，但一律经 scaled() 缩放。
// 纯头文件（内联），无新 TU。
#pragma once

#include <algorithm>

namespace lw::ui {

constexpr float kMinUiScale = 0.6f;
constexpr float kMaxUiScale = 2.0f;

// 主面板宽度基准（uiScale=1；默认 2.0 → 580，面板不压地图左缘）。
constexpr float kMainPanelWidth = 290.0f;
inline float mainPanelWidth(float uiScale) {
    return std::max(240.0f, kMainPanelWidth * uiScale);  // 下限 240px，防过窄挤压
}

// 通用控件基准宽度（长条选项区/滑条；长度随缩放倍率）。
constexpr float kSliderWidth = 220.0f;
constexpr float kButtonWidth = 140.0f;
constexpr float kDropdownWidth = 120.0f;

// 基准尺寸 × 缩放（唯一缩放公式；新面板一律用它，勿再手写 `x * uiScale`）。
inline float scaled(float base, float uiScale) { return base * uiScale; }

}  // namespace lw::ui
