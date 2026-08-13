// PanelManager.h — 多面板管理器（开发计划 P3，思路 6.0）。
// 纯逻辑（注册/可见性过滤/开关互斥/布局读写）内联在本头文件 → 可脱离 UI 层单测；
// 每帧 ImGui 窗口绘制（PanelManager::draw）定义在 PanelManager.cpp（app lib）。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Options.h"   // lw::PanelLayout（布局持久化）
#include "ui/panels/Panel.h"

namespace lw::ui {

class PanelManager {
public:
    // 注册面板（主面板先注册；所有权归 manager，按注册顺序保存）。
    void registerPanel(std::unique_ptr<Panel> panel) { panels_.push_back(std::move(panel)); }

    // 全部已注册面板（按注册顺序；主面板固定 index 0）。
    std::vector<Panel*> panels() const {
        std::vector<Panel*> out;
        out.reserve(panels_.size());
        for (const auto& p : panels_) out.push_back(p.get());
        return out;
    }

    // 可见面板（含主面板，主面板固定最前）。
    std::vector<Panel*> visiblePanels() const {
        std::vector<Panel*> out;
        for (const auto& p : panels_)
            if (p->state.visible) out.push_back(p.get());
        return out;
    }

    // 按 id 查面板（nullptr = 未注册）。
    Panel* panel(const std::string& id) const {
        for (const auto& p : panels_)
            if (p->state.id == id) return p.get();
        return nullptr;
    }

    // 切换可见性。主面板不可隐藏（no-op）；其余面板按 v 设显隐。
    void setVisible(const std::string& id, bool v) {
        Panel* p = panel(id);
        if (!p || p->state.mainPanel) return;
        p->state.visible = v;
    }

    // 布局导出（持久化用）：全部非主面板的 id/visible/pos/size（主面板固定，无需持久化）。
    std::vector<lw::PanelLayout> layout() const {
        std::vector<lw::PanelLayout> out;
        for (const auto& p : panels_) {
            if (p->state.mainPanel) continue;
            out.push_back({p->state.id, p->state.visible, p->state.x, p->state.y, p->state.w,
                           p->state.h});
        }
        return out;
    }

    // 布局导入：按 id 匹配回填 pos/size/visible（主面板 visible 恒 true，忽略）。
    void applyLayout(const std::vector<lw::PanelLayout>& layout) {
        for (const auto& l : layout) {
            Panel* p = panel(l.id);
            if (!p) continue;
            p->state.x = l.x;
            p->state.y = l.y;
            p->state.w = l.w;
            p->state.h = l.h;
            if (!p->state.mainPanel) p->state.visible = l.visible;
        }
    }

    // 每帧绘制全部可见面板：主面板（固定左侧、自身负责窗口）+ 可移动面板窗口
    // （统一可拖动 + 标题栏"隐藏"按钮 + 位置/大小回写）。定义在 PanelManager.cpp（依赖 ImGui）。
    void draw(PanelCtx& ctx);

private:
    std::vector<std::unique_ptr<Panel>> panels_;
};

}  // namespace lw::ui
