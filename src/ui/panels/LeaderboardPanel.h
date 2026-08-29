// LeaderboardPanel.h — 可移动/可隐藏的势力排行榜面板（开发计划 3.7）。
#pragma once

#include <string>
#include <vector>

#include "ui/panels/PanelManager.h"

namespace lw {
class Faction;
}

namespace lw::ui {

class LeaderboardPanel : public Panel {
public:
    LeaderboardPanel();
    const std::string& id() const override { return state.id; }
    void draw(PanelCtx& ctx) override;

private:
    std::vector<const Faction*> ranked_;
};

}  // namespace lw::ui
