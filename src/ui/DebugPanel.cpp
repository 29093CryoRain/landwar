// DebugPanel.cpp — 左侧主信息面板实现（Phase 8 合并版；P3 主面板）。
#include "ui/DebugPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "app/InputManager.h"  // app::Selection 完整定义
#include "core/Simulation.h"
#include "sim/components.h"
#include "ui/UiScale.h"
#include "ui/UiText.h"
#include "world/Faction.h"

namespace lw::ui {

namespace {

const char* armyTypeName(ArmyType t) {
    switch (t) {
        case ArmyType::normal: return "普通";
        case ArmyType::vanguard: return "先锋";
        case ArmyType::pioneer: return "开拓";
        case ArmyType::laser: return "激光";
        case ArmyType::bomb: return "爆炸";
        case ArmyType::mine: return "地雷";
        case ArmyType::pistol: return "手枪";
        case ArmyType::shotgun: return "霰弹";
        default: return "?";
    }
}

}  // namespace

DebugPanel::DebugPanel() {
    state.id = "main";           // 唯一 id（主面板标志；布局持久化跳过主面板）
    state.title = "主面板";
    state.mainPanel = true;      // 固定左侧、不可移动/隐藏
    state.movable = false;
    state.visible = true;        // 主面板恒显示
}

void DebugPanel::draw(PanelCtx& ctx) {
    // 只读模拟引用 + 双向控制；所有成员须在 ctx 中非空（Application 每帧构建）。
    const Simulation& sim = *ctx.sim;
    const app::Selection& sel = *ctx.selection;
    const std::uint32_t seed = ctx.seed;
    const DebugCounts& counts = *ctx.counts;
    UiControls& ctrl = *ctx.controls;
    UiRequests& req = *ctx.requests;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // 面板宽度随 UI 缩放（2026-08 统一走 UiScale：基准 290 → 2.0×=580 ≤ panelWidth 600）。
    const float w = mainPanelWidth(ctrl.uiScale);
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, display.y));
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("###info-panel", nullptr, flags);

    char buf[128];
    // 内容滚动区（除底部 UI 缩放条外全部内容；2026-08 修复：此前缩放条用 SetCursorPosY
    // 定位在内容流底部，内容超高滚动时会随内容滚到上方——改为独立 Child 滚动区 +
    // 窗口底部固定区，滚动条只滚内容，缩放条恒在底部）。
    const float bottomH = 58.0f;
    ImGui::BeginChild("###main-scroll", ImVec2(0.0f, std::max(0.0f, display.y - bottomH)), false,
                      0);

    ImGui::TextUnformatted("领土战争");
    ImGui::Separator();
    std::snprintf(buf, sizeof(buf), "时间: %llu   实体: 兵 %d · 特效 %d",
                  static_cast<unsigned long long>(sim.tickCount()), counts.armyTotal,
                  counts.effectCount);
    ImGui::TextUnformatted(buf);
    // P6：主面板同时显示地图随机种子与主随机种子（RNG 分离）。
    std::snprintf(buf, sizeof(buf), "地图种子: %u    主种子: %u", ctx.mapSeed, seed);
    ImGui::TextUnformatted(buf);

    // ---- 面板开关区（P3：各注册可移动面板的 checkbox）----
    drawPanelToggles(ctx);

    // ---- 速度 / 暂停（渲染节奏，不改模拟）----
    ImGui::Separator();
    ImGui::TextUnformatted("速度 / 暂停");
    // 倍速档位含 <1x（0.25/0.5，隔帧执行逻辑帧，测试用）；+/- 循环步进。
    const float steps[6] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    int cur = 0;
    for (int i = 0; i < 6; ++i) {
        if (std::fabs(ctrl.speed - steps[i]) < 0.01) cur = i;
    }
    ImGui::Text("倍速: %gx", ctrl.speed);
    if (ImGui::Button("0.25x")) ctrl.speed = 0.25;
    ImGui::SameLine();
    if (ImGui::Button("0.5x")) ctrl.speed = 0.5;
    ImGui::SameLine();
    if (ImGui::Button("1x")) ctrl.speed = 1.0;
    ImGui::SameLine();
    if (ImGui::Button("2x")) ctrl.speed = 2.0;
    ImGui::SameLine();
    if (ImGui::Button("4x")) ctrl.speed = 4.0;
    ImGui::SameLine();
    if (ImGui::Button("8x")) ctrl.speed = 8.0;
    if (ImGui::Button("-")) ctrl.speed = steps[(cur > 0) ? cur - 1 : 0];
    ImGui::SameLine();
    if (ImGui::Button("+")) ctrl.speed = steps[(cur < 5) ? cur + 1 : 5];
    ImGui::SameLine();
    ImGui::Checkbox("暂停 (空格)", &ctrl.paused);
    ImGui::SameLine();
    if (ImGui::Button("步进 (F6)")) req.stepFrame = true;

    // ---- 玩家模式区（P2：兵种图标栏 + 经济 + 已选摘要 + P15 指定首都）----
    if (ctx.sheet) drawPlayerSection(sim, req, ctx.controls, *ctx.sheet);

    // ---- 点选详情 ----
    ImGui::Separator();
    drawSelection(sim, sel);

    // ---- 调试动作 ----
    ImGui::Separator();
    if (ImGui::Button("重载配置 (F5)")) req.reloadConfig = true;
    if (ImGui::Button("输出终局摘要")) req.endgameSummary = true;

    // ---- 操作提示（分多行，避免超宽被截断，用户反馈 Phase 8）----
    ImGui::Separator();
    ImGui::TextUnformatted("空格暂停 · ESC退出确认 · F6步进");
    ImGui::TextUnformatted("1/2/3/4 倍速 · +/- 调档(0.25~8x) · F5 重载配置 · F7 重烘焙渲染色");
    ImGui::TextUnformatted("滚轮缩放 · 右拖平移 · 左键点选 · F12 截图");

    ImGui::EndChild();

    // ---- UI 缩放（固定在窗口底部，不随内容滚动；2026-08 修复）----
    // 长度随缩放倍率（直观反映当前值，Phase 9 反馈）。
    ImGui::SetCursorPosY(display.y - bottomH);
    ImGui::SetNextItemWidth(scaled(kSliderWidth, ctrl.uiScale));
    ImGui::SliderFloat("UI缩放", &ctrl.uiScale, kMinUiScale, kMaxUiScale, "%.2f");
    ImGui::End();
}

void DebugPanel::drawPanelToggles(PanelCtx& ctx) {
    ImGui::Separator();
    ImGui::TextUnformatted("面板");
    if (!ctx.manager) {
        ImGui::TextUnformatted("(无其他面板)");
        return;
    }
    bool any = false;
    for (Panel* p : ctx.manager->panels()) {
        if (p->state.mainPanel) continue;  // 主面板自身不列在开关里（恒显示）
        any = true;
        bool vis = p->state.visible;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s##toggle-%s", p->state.title.c_str(),
                      p->state.id.c_str());
        if (ImGui::Checkbox(buf, &vis)) p->state.visible = vis;  // 开关即显示/隐藏
    }
    if (!any) ImGui::TextUnformatted("(无其他面板)");
}

int DebugPanel::playerFactionId(const Simulation& sim) {
    // 以运行时 Faction.aiId 为准（读档后也正确，options 不随快照）。
    for (int id : sim.selectedFactionIds()) {
        if (sim.faction(id).aiId == 1) return id;
    }
    return 0;
}

void DebugPanel::drawPlayerSection(const Simulation& sim, UiRequests& req, UiControls* controls,
                                   const render::SpriteSheet& sheet) {
    const int playerFid = playerFactionId(sim);
    if (playerFid == 0) return;  // 非玩家模式不显示
    const auto& f = sim.faction(playerFid);
    const auto& intent = sim.playerIntent();

    ImGui::Separator();
    ImGui::TextUnformatted("玩家 ·");
    ImGui::SameLine();
    drawFactionName(sim, playerFid);

    // 经济（留存值库存）
    char buf[64];
    std::snprintf(buf, sizeof(buf), "经济 %.2f", f.economy);
    ImGui::TextUnformatted(buf);

    // 科技进度条（P8，思路"科技细节"：进度条表明科技填了当前阈值的多少）。
    if (f.tech.threshold > 0.0) {
        const double ratio = f.tech.points / f.tech.threshold;
        const float frac = ratio > 1.0 ? 1.0f : (ratio < 0.0 ? 0.0f : static_cast<float>(ratio));
        std::snprintf(buf, sizeof(buf), "科技 %.0f/%.0f", f.tech.points, f.tech.threshold);
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), buf);
    }

    // ---- 兵种图标栏：最左空框（不生产）+ 8 个兵种子图（玩家势力色，统一 tint 管线），选中高亮，下方标成本。
    // 成本 = 有效造价 = f.armyCost × mods.costMult（P11 各势力同价 = 定义 baseCost；P8 节流
    // 科技降低 → 面板显示折扣后价格，与 PlayerAI/ProductionSystem 扣费一致）。
    // P9 手枪/霰弹走 unitRect（army_base.png 行 9/10 贴图），选中高亮与其余兵种一致。
    // 2026-08 手动布局（表格方案多轮迭代后放弃）：完全控制尺寸/间距/行宽——
    //   - 可用宽 = 面板宽 − 2×WindowPadding − ScrollbarSize（主面板内容超高时滚动条占位，
    //     不预留则图标栏右缘被滚动条挤压/溢出——"间隔不一致/不占满"根因）；
    //   - 列间距 gap = scaled(4, uiScale)（随缩放，列间/首尾统一 → 间隔均匀）；
    //   - 图标 = (可用宽 − 8×gap) / 9 → **恰好占满一整行**（用户要求放大到满行）；
    //   - FramePadding 压 0：ImageButton 外框 = image_size+2×FramePadding（imgui 源码
    //     ImageButtonEx），Button 的 size 即外框 → 空框与兵种框严格同大。
    const int tw = sheet.textureWidth(), th = sheet.textureHeight();
    const float uiScaleF = controls ? controls->uiScale : 1.0f;
    const float scrollW = ImGui::GetStyle().ScrollbarSize;  // 滚动条占位（内容超高时出现）
    const float availW = mainPanelWidth(uiScaleF) - 2.0f * ImGui::GetStyle().WindowPadding.x
                         - scrollW;
    const float gap = scaled(4.0f, uiScaleF);   // 列间距（随缩放；列间与首尾一致）
    const float iconSize = std::max(8.0f, (availW - 8.0f * gap) / 9.0f);
    char idBuf[24];
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    const float x0 = ImGui::GetCursorPosX();
    const float y0 = ImGui::GetCursorPosY();
    // 空框 = 不生产（selectedType == -1）。
    const bool noneSelected = (intent.selectedType == -1);
    if (noneSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.6f, 1.0f, 0.5f));
    ImGui::SetCursorPos(ImVec2(x0, y0));
    if (ImGui::Button("##none", ImVec2(iconSize, iconSize))) req.selectNone = true;
    if (noneSelected) ImGui::PopStyleColor();
    for (int t = 0; t < kArmyTypeCount; ++t) {
        // 双色渲染（视觉工程改进 ⑫）：统一 army_base.png，
        // 势力版 = 该势力 (主色, 副色) 合成纹理。
        ImGui::SetCursorPos(ImVec2(x0 + static_cast<float>(t + 1) * (iconSize + gap), y0));
        const ImTextureID tid = reinterpret_cast<ImTextureID>(sheet.texture(playerFid - 1));
        const SDL_Rect r = sheet.unitRect(t);
        const ImVec2 u0(r.x / static_cast<float>(tw), r.y / static_cast<float>(th));
        const ImVec2 u1((r.x + r.w) / static_cast<float>(tw),
                        (r.y + r.h) / static_cast<float>(th));
        const bool selected = (intent.selectedType == t);
        // 选中 → 蓝色背景高亮；未选 → 透明。
        const ImVec4 bg =
            selected ? ImVec4(0.35f, 0.6f, 1.0f, 0.5f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        std::snprintf(idBuf, sizeof(idBuf), "##unit%d", t);
        if (ImGui::ImageButton(idBuf, tid, ImVec2(iconSize, iconSize), u0, u1, bg)) {
            req.selectUnitType = t;  // Application 消费 → 写入 PlayerIntent
        }
    }
    // 成本行（与图标同列对齐；空框列 "—"）。
    const float yCost = y0 + iconSize + scaled(2.0f, uiScaleF);
    ImGui::SetCursorPos(ImVec2(x0, yCost));
    ImGui::TextUnformatted("—");
    for (int t = 0; t < kArmyTypeCount; ++t) {
        ImGui::SetCursorPos(ImVec2(x0 + static_cast<float>(t + 1) * (iconSize + gap), yCost));
        std::snprintf(buf, sizeof(buf), "%.0f", f.armyCost[static_cast<size_t>(t)]
                                                      * f.mods.costMult[static_cast<size_t>(t)]);
        ImGui::TextUnformatted(buf);
    }
    ImGui::PopStyleVar();  // FramePadding（压零统一空框/兵种框尺寸）
    // 恢复光标到成本行之后（手动 SetCursorPos 后布局流继续）。
    ImGui::SetCursorPosY(yCost + ImGui::GetTextLineHeight() + scaled(2.0f, uiScaleF));

    // 已选摘要：兵种名 + 产兵城。
    const char* typeName = (intent.selectedType >= 0 && intent.selectedType < kArmyTypeCount)
                               ? armyTypeName(static_cast<ArmyType>(intent.selectedType))
                               : "(不产)";
    if (intent.cityX >= 0.0) {
        // 2026-08-07：意图坐标 = 城市几何中心（可为 .5）→ 保留一位小数。
        std::snprintf(buf, sizeof(buf), "兵种 %s · 产兵城 (%.1f,%.1f)", typeName, intent.cityX,
                      intent.cityY);
    } else {
        std::snprintf(buf, sizeof(buf), "兵种 %s · 产兵城 (未选)", typeName);
    }
    ImGui::TextUnformatted(buf);
    ImGui::TextUnformatted("数字键 1-8 选兵种 · ` 不产 · 左键点己方城市选产兵城");

    // ---- 首都（P15，思路 9.2）：状态 + 指定首都（仅无正式首都时可用）----
    ImGui::Separator();
    if (f.capitalState.capitalCityId >= 0) {
        // 有正式首都：显示位置（不可迁都，spec："依据迁都是否可用，处理显示方式"→ 按钮置灰）。
        const auto& cap = sim.map().city(static_cast<size_t>(f.capitalState.capitalCityId));
        std::snprintf(buf, sizeof(buf), "首都 (%.0f,%.0f)", cap.centerX(), cap.centerY());
        ImGui::TextUnformatted(buf);
        ImGui::BeginDisabled();
        if (ImGui::Button("指定首都")) req.toggleCapitalDesignation = true;
        ImGui::EndDisabled();
        ImGui::TextUnformatted("有正式首都 · 不可迁都");
    } else {
        // 无正式首都（沦陷窗口内/立即迁都态）：可指定候补新都。
        if (f.capitalState.designatedCityId >= 0) {
            const auto& cap = sim.map().city(static_cast<size_t>(f.capitalState.designatedCityId));
            std::snprintf(buf, sizeof(buf), "候补新都 (%.0f,%.0f)", cap.centerX(), cap.centerY());
            ImGui::TextUnformatted(buf);
        } else {
            ImGui::TextUnformatted("无首都 · 请指定候补新都");
        }
        // 迁都冷却期（2026-08 细节改进）：显示文本 + 剩余冷却秒数（保留 1 位小数）。
        // 窗口 = [lostTick, lostTick + relocationDelayTicks)；窗口内迁都不可立即执行。
        if (f.capitalState.lostTick > 0) {
            const double delayTicks = sim.config().capital.relocationDelayTicks;
            const double remainTicks =
                static_cast<double>(f.capitalState.lostTick) + delayTicks
                - static_cast<double>(sim.tickCount());
            if (remainTicks > 0.0) {
                const double remainSec = remainTicks / sim.config().sim.tickRate;
                std::snprintf(buf, sizeof(buf), "迁都冷却中 · 剩余 %.1fs", remainSec);
                ImGui::TextUnformatted(buf);
            }
        }
        const bool designating = controls && controls->capitalDesignating;
        if (ImGui::Button(designating ? "取消指定首都" : "指定首都")) req.toggleCapitalDesignation = true;
        ImGui::TextUnformatted(designating ? "点击己方城市 → 指定/立即迁都" : "左键点己方城市指定候补新都");
    }
}

void DebugPanel::drawSelection(const Simulation& sim, const app::Selection& sel) {
    ImGui::TextUnformatted("点选");
    char buf[128];
    switch (sel.kind) {
        case app::Selection::Kind::None:
            ImGui::TextUnformatted("(无)");
            break;
        case app::Selection::Kind::Army: {
            const auto& reg = sim.registry();
            if (!reg.valid(sel.army)) {
                ImGui::TextUnformatted("兵已销毁");
                break;
            }
            const auto& p = reg.get<comp::Position>(sel.army);
            const auto& speed = reg.get<comp::Speed>(sel.army);
            const auto& onLand = reg.get<comp::OnLand>(sel.army);
            const auto& collider = reg.get<comp::Collider>(sel.army);
            const int fid = reg.get<comp::FactionId>(sel.army).value;
            const ArmyType t = reg.get<comp::UnitType>(sel.army).type;
            ImGui::Text("%s ·", armyTypeName(t));
            ImGui::SameLine();
            drawFactionName(sim, fid);
            std::snprintf(buf, sizeof(buf), "位置 (%.2f, %.2f)  速度 %.2f", p.x, p.y, speed.value);
            ImGui::TextUnformatted(buf);
            std::snprintf(buf, sizeof(buf), "%s · 半径 %.2f", onLand.value ? "陆上" : "海上",
                          collider.radius);
            ImGui::TextUnformatted(buf);
            break;
        }
        case app::Selection::Kind::Cell: {
            if (sel.cellIndex < 0 || sel.cellIndex >= sim.map().cellCount()) {
                ImGui::TextUnformatted("格已失效");
                break;
            }
            const MapCell& cell = sim.map().atIndex(sel.cellIndex);
            std::snprintf(buf, sizeof(buf), "格 (%d, %d)  %s%s · 归属", sel.cellX, sel.cellY,
                          cell.land ? "陆地" : "海上", cell.cityId >= 0 ? "/城市" : "");
            ImGui::TextUnformatted(buf);
            ImGui::SameLine();
            if (cell.belongi == 0) ImGui::TextUnformatted("中立");
            else drawFactionName(sim, cell.belongi);
            break;
        }
    }
}

}  // namespace lw::ui
