// test_panels.cpp — 面板系统单测（开发计划 P3：PanelManager 注册/可见性/开关互斥 + Options.panels 往返）。
// PanelManager 纯逻辑内联在 ui/panels/PanelManager.h → 测试可脱离 UI/渲染层直接验证；
// 可移动面板窗口绘制（PanelManager::draw，依赖 ImGui）不在测试范围。
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/Options.h"
#include "ui/panels/PanelManager.h"

namespace {

using namespace lw;
using lw::ui::Panel;
using lw::ui::PanelCtx;
using lw::ui::PanelManager;

// 测试用最小面板（仅携带 state）。
struct DummyPanel : Panel {
    explicit DummyPanel(std::string pid) {
        state.id = std::move(pid);
        state.title = state.id;
    }
    const std::string& id() const override { return state.id; }
    void draw(PanelCtx&) override {}
};

// ---- PanelManager：注册 / 可见性过滤 / 开关互斥 ----

TEST(PanelManager, RegisterKeepsOrderMainFirst) {
    PanelManager mgr;
    auto main = std::make_unique<DummyPanel>("main");
    main->state.mainPanel = true;
    main->state.visible = true;
    mgr.registerPanel(std::move(main));
    mgr.registerPanel(std::make_unique<DummyPanel>("msg"));
    mgr.registerPanel(std::make_unique<DummyPanel>("stats"));

    const auto ps = mgr.panels();
    ASSERT_EQ(ps.size(), 3u);
    EXPECT_EQ(ps[0]->state.id, "main");  // 主面板固定最前
    EXPECT_EQ(ps[1]->state.id, "msg");
    EXPECT_EQ(ps[2]->state.id, "stats");
    EXPECT_NE(mgr.panel("msg"), nullptr);
    EXPECT_EQ(mgr.panel("nonexistent"), nullptr);
}

TEST(PanelManager, VisiblePanelsFiltersHidden) {
    PanelManager mgr;
    auto main = std::make_unique<DummyPanel>("main");
    main->state.mainPanel = true;
    main->state.visible = true;
    mgr.registerPanel(std::move(main));
    auto msg = std::make_unique<DummyPanel>("msg");
    msg->state.visible = false;
    mgr.registerPanel(std::move(msg));
    auto stats = std::make_unique<DummyPanel>("stats");
    stats->state.visible = true;
    mgr.registerPanel(std::move(stats));

    const auto vs = mgr.visiblePanels();
    ASSERT_EQ(vs.size(), 2u);
    EXPECT_EQ(vs[0]->state.id, "main");  // 主面板固定最前
    EXPECT_EQ(vs[1]->state.id, "stats");
}

TEST(PanelManager, MainPanelCannotBeHiddenOrMoved) {
    PanelManager mgr;
    auto main = std::make_unique<DummyPanel>("main");
    main->state.mainPanel = true;
    main->state.movable = false;
    main->state.visible = true;
    mgr.registerPanel(std::move(main));
    mgr.registerPanel(std::make_unique<DummyPanel>("msg"));

    mgr.setVisible("main", false);  // 主面板不可隐藏（no-op）
    EXPECT_TRUE(mgr.panel("main")->state.visible);
    EXPECT_FALSE(mgr.panel("main")->state.movable);
    EXPECT_FALSE(mgr.panel("msg")->state.movable);  // 默认不可移动（注册时未置）
}

TEST(PanelManager, MovablePanelToggleVisibility) {
    PanelManager mgr;
    auto msg = std::make_unique<DummyPanel>("msg");
    msg->state.movable = true;
    mgr.registerPanel(std::move(msg));
    mgr.registerPanel(std::make_unique<DummyPanel>("stats"));

    EXPECT_FALSE(mgr.panel("msg")->state.visible);  // 默认隐藏
    mgr.setVisible("msg", true);
    EXPECT_TRUE(mgr.panel("msg")->state.visible);
    mgr.setVisible("msg", false);
    EXPECT_FALSE(mgr.panel("msg")->state.visible);
    mgr.setVisible("nonexistent", true);  // 未注册 id → no-op
    EXPECT_EQ(mgr.panel("nonexistent"), nullptr);
}

// ---- PanelManager：布局导出/导入（持久化链路）----

TEST(PanelManager, LayoutExportsOnlyMovablePanels) {
    PanelManager mgr;
    auto main = std::make_unique<DummyPanel>("main");
    main->state.mainPanel = true;
    mgr.registerPanel(std::move(main));
    auto msg = std::make_unique<DummyPanel>("msg");
    msg->state.visible = true;
    msg->state.x = 100.0f;
    msg->state.y = 200.0f;
    msg->state.w = 340.0f;
    msg->state.h = 240.0f;
    mgr.registerPanel(std::move(msg));

    const auto layout = mgr.layout();
    ASSERT_EQ(layout.size(), 1u);              // 主面板不入持久化
    EXPECT_EQ(layout[0].id, "msg");
    EXPECT_TRUE(layout[0].visible);
    EXPECT_FLOAT_EQ(layout[0].x, 100.0f);
    EXPECT_FLOAT_EQ(layout[0].y, 200.0f);
    EXPECT_FLOAT_EQ(layout[0].w, 340.0f);
    EXPECT_FLOAT_EQ(layout[0].h, 240.0f);
}

TEST(PanelManager, ApplyLayoutRestoresByIdIgnoresUnknown) {
    PanelManager mgr;
    mgr.registerPanel(std::make_unique<DummyPanel>("msg"));
    mgr.registerPanel(std::make_unique<DummyPanel>("stats"));

    std::vector<lw::PanelLayout> layout;
    layout.push_back({"msg", true, 500.0f, 60.0f, 400.0f, 300.0f});
    layout.push_back({"unknown", false, 0.0f, 0.0f, 0.0f, 0.0f});  // 未注册 → 忽略
    mgr.applyLayout(layout);

    EXPECT_TRUE(mgr.panel("msg")->state.visible);
    EXPECT_FLOAT_EQ(mgr.panel("msg")->state.x, 500.0f);
    EXPECT_FLOAT_EQ(mgr.panel("msg")->state.y, 60.0f);
    EXPECT_FLOAT_EQ(mgr.panel("msg")->state.w, 400.0f);
    EXPECT_FLOAT_EQ(mgr.panel("msg")->state.h, 300.0f);
    EXPECT_FALSE(mgr.panel("stats")->state.visible);  // 未出现在布局 → 保持默认隐藏
}

TEST(PanelManager, ApplyLayoutNeverHidesMainPanel) {
    PanelManager mgr;
    auto main = std::make_unique<DummyPanel>("main");
    main->state.mainPanel = true;
    main->state.visible = true;
    mgr.registerPanel(std::move(main));

    std::vector<lw::PanelLayout> layout;
    layout.push_back({"main", false, 0.0f, 0.0f, 0.0f, 0.0f});  // 即使布局写 false
    mgr.applyLayout(layout);
    EXPECT_TRUE(mgr.panel("main")->state.visible);  // 主面板恒显示
}

// ---- Options.panels 往返（json 键严格对称）----

TEST(OptionsPanels, EmptyByDefaultAndRoundTrips) {
    lw::Options o;
    EXPECT_TRUE(o.panels.empty());
    const lw::Options b = lw::Options::loadFromJson(o.toJson());
    EXPECT_TRUE(b.panels.empty());
}

TEST(OptionsPanels, JsonRoundTripPreservesLayout) {
    lw::Options a;
    a.panels.push_back({"message", true, 120.0f, 300.0f, 340.0f, 240.0f});
    a.panels.push_back({"stats", false, 0.0f, 0.0f, 0.0f, 0.0f});

    const lw::Options b = lw::Options::loadFromJson(a.toJson());
    ASSERT_EQ(b.panels.size(), 2u);
    EXPECT_EQ(b.panels[0].id, "message");
    EXPECT_TRUE(b.panels[0].visible);
    EXPECT_FLOAT_EQ(b.panels[0].x, 120.0f);
    EXPECT_FLOAT_EQ(b.panels[0].y, 300.0f);
    EXPECT_FLOAT_EQ(b.panels[0].w, 340.0f);
    EXPECT_FLOAT_EQ(b.panels[0].h, 240.0f);
    EXPECT_EQ(b.panels[1].id, "stats");
    EXPECT_FALSE(b.panels[1].visible);

    // 未设置的其他选项字段保持默认（往返无损）。
    EXPECT_EQ(b.map.file, "data/map_bigIslands.bmp");
    EXPECT_TRUE(b.factions[0].enabled);
}

TEST(OptionsPanels, MissingPanelsKeyFallsBackToEmpty) {
    const lw::Options o = lw::Options::loadFromJson("{\"factions\":[]}");
    EXPECT_TRUE(o.panels.empty());
}

}  // namespace
