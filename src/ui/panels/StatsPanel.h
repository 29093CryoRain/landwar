// StatsPanel.h — 统计面板（开发计划 P11，思路 8）。挂 P3 PanelManager 的可移动面板。
// 展示 sim.stats()：势力×兵种（可按势力切换）、势力汇总、兵种汇总、统一截止时刻。
// 人均（kills/produced、lands/produced）为展示层派生（0 除保护）。纯展示，不改模拟状态。
#pragma once

#include <string>

#include "ui/panels/PanelManager.h"

namespace lw::ui {

class StatsPanel : public Panel {
public:
    StatsPanel();
    const std::string& id() const override { return state.id; }
    void draw(PanelCtx& ctx) override;
};

}  // namespace lw::ui
