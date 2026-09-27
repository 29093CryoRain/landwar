// LeaderboardPanel.h — 可移动/可隐藏的排行榜面板：势力榜 + 联盟榜（两种模式）。
// 势力榜：一行 = 一个势力，按领地降序。
// 联盟榜：一行 = 一个联盟（未入盟势力视作"单势力联盟"），聚合列按成员求和，按领地降序。
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
    enum class Mode { Faction, Alliance };

    explicit LeaderboardPanel(Mode mode);
    const std::string& id() const override { return state.id; }
    void draw(PanelCtx& ctx) override;

private:
    // 联盟榜一行：成员 + 按成员求和/取最大的聚合列。
    struct AllianceRow {
        std::vector<int> members;
        int land = 0;
        int cities = 0;
        int armies = 0;
        double economyRate = 0.0;  // 每 tick 经济产出（显示时 × tickRate）
    };

    void drawFactionBoard(PanelCtx& ctx);
    void drawAllianceBoard(PanelCtx& ctx);

    Mode mode_;
    std::vector<const Faction*> ranked_;       // 势力榜排序缓存（避免每帧分配）
    std::vector<AllianceRow> allianceRows_;    // 联盟榜排序缓存
};

}  // namespace lw::ui
