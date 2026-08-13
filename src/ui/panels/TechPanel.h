// TechPanel.h — 科技面板（开发计划 P8，思路 6.2"每个势力的科技需要放在科技面板上"）。
// 挂 P3 PanelManager 的可移动面板。每势力一子节：科技点/阈值（进度）、科技等级、已拥有科技+等级。
// 纯展示，不改模拟状态。
#pragma once

#include <string>

#include "ui/panels/PanelManager.h"

namespace lw::ui {

class TechPanel : public Panel {
public:
    TechPanel();
    const std::string& id() const override { return state.id; }
    void draw(PanelCtx& ctx) override;
};

}  // namespace lw::ui
