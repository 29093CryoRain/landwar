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

    // ---- 速度 / 暂停 / UI 缩放（渲染节奏，不改模拟）----
    ImGui::Separator();
    ImGui::TextUnformatted("速度 / 暂停 / UI");
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

    // ---- 排行榜（合并原 PanelRenderer 的势力排序表）----
    ImGui::Separator();
    drawLeaderboard(sim, counts);

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
    ImGui::TextUnformatted("空格 / ESC双击 暂停 · F6 步进");
    ImGui::TextUnformatted("1/2/3/4 倍速 · +/- 调档(0.25~8x) · F5 重载配置");
    ImGui::TextUnformatted("滚轮缩放 · 右拖平移 · 左键点选 · F12 截图");

    // ---- UI 缩放（放最下方，Phase 9 用户反馈）----
    // 锚定窗口底部固定高度 → 竖直位置不随上方内容/缩放倍率变化；长度随缩放倍率（直观反映当前值）。
    ImGui::SetCursorPosY(display.y - 58.0f);
    ImGui::SetNextItemWidth(scaled(kSliderWidth, ctrl.uiScale));  // 长度随缩放倍率（Phase 9 反馈）
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
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
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
    ImGui::PushStyleColor(ImGuiCol_Text, rgbToImVec4(f.color));
    ImGui::Text("玩家 · %s", factionShortName(playerFid));
    ImGui::PopStyleColor();

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

    // 兵种图标栏：最左空框（不生产）+ 8 个兵种子图（玩家势力色，统一 tint 管线），选中高亮，下方标成本。
    // 成本 = 有效造价 = f.armyCost × mods.costMult（P11 各势力同价 = 定义 baseCost；P8 节流
    // 科技降低 → 面板显示折扣后价格，与 PlayerAI/ProductionSystem 扣费一致）。
    // P9 手枪/霰弹走 unitRect（army_base.png 行 9/10 贴图），选中高亮与其余兵种一致。
    const int tw = sheet.textureWidth(), th = sheet.textureHeight();
    const float iconSize = 36.0f;
    char idBuf[24];
    if (ImGui::BeginTable("##unitbar", kArmyTypeCount + 1, ImGuiTableFlags_SizingFixedFit)) {
        // 空框 = 不生产（selectedType == -1）。
        ImGui::TableNextColumn();
        const bool noneSelected = (intent.selectedType == -1);
        if (noneSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.6f, 1.0f, 0.5f));
        if (ImGui::Button("##none", ImVec2(iconSize, iconSize))) req.selectNone = true;
        if (noneSelected) ImGui::PopStyleColor();
        for (int t = 0; t < kArmyTypeCount; ++t) {
            ImGui::TableNextColumn();
            // 玩家势力的签名兵种用特殊版贴图（原样彩色），其余用该势力色相旋转的基础版。
            const bool special = (playerFid == kSpecialUnitFaction[t]);
            const ImTextureID tid = reinterpret_cast<ImTextureID>(
                special ? sheet.specialTexture() : sheet.texture(playerFid - 1));
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
        // 成本行：空框列显示 "—"。
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("—");
        for (int t = 0; t < kArmyTypeCount; ++t) {
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%.0f", f.armyCost[static_cast<size_t>(t)]
                                                          * f.mods.costMult[static_cast<size_t>(t)]);
            ImGui::TextUnformatted(buf);
        }
        ImGui::EndTable();
    }

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

void DebugPanel::drawLeaderboard(const Simulation& sim, const DebugCounts& counts) {
    ImGui::TextUnformatted("排行榜（按领地降序）");
    // 势力 1..8 按 landCount 降序（原版 ranked_fact + std::sort）。
    ranked_.clear();
    for (int id = 1; id <= kPlayerFactionCount; ++id) ranked_.push_back(&sim.faction(id));
    std::sort(ranked_.begin(), ranked_.end(),
              [](const Faction* a, const Faction* b) { return a->landCount > b->landCount; });

    if (ImGui::BeginTable("##leaderboard", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("名次");
        ImGui::TableSetupColumn("势力");
        ImGui::TableSetupColumn("领地");
        ImGui::TableSetupColumn("城市");
        ImGui::TableSetupColumn("兵力");
        ImGui::TableSetupColumn("科技");  // P8：科技等级（所有已拥有科技等级之和）
        ImGui::TableHeadersRow();
        char buf[32];
        for (int i = 0; i < kPlayerFactionCount; ++i) {
            const Faction* f = ranked_[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", i + 1);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, rgbToImVec4(f->color));
            ImGui::TextUnformatted(factionShortName(f->id));  // 单字颜色名（红/黄/青/…）
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", f->landCount);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", f->cityCount);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", counts.armyPerFaction[static_cast<size_t>(f->id)]);
            ImGui::TextUnformatted(buf);
            ImGui::TableNextColumn();
            std::snprintf(buf, sizeof(buf), "%d", f->techLevel());
            ImGui::TextUnformatted(buf);
        }
        ImGui::EndTable();
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
            std::snprintf(buf, sizeof(buf), "%s · %s", armyTypeName(t), factionShortName(fid));
            ImGui::TextUnformatted(buf);
            std::snprintf(buf, sizeof(buf), "位置 (%.2f, %.2f)  速度 %.2f", p.x, p.y, speed.value);
            ImGui::TextUnformatted(buf);
            std::snprintf(buf, sizeof(buf), "%s · 半径 %.2f", onLand.value ? "陆上" : "海上",
                          collider.radius);
            ImGui::TextUnformatted(buf);
            break;
        }
        case app::Selection::Kind::Cell: {
            const MapCell& cell = sim.map().at(sel.cellX, sel.cellY);
            // 归属显示势力名（单字颜色名；0 = 中立）。
            const char* owner = cell.belongi == 0 ? "中立" : factionShortName(cell.belongi);
            std::snprintf(buf, sizeof(buf), "格 (%d, %d)  %s%s · 归属 %s", sel.cellX, sel.cellY,
                          cell.land ? "陆地" : "海上", cell.cityId >= 0 ? "/城市" : "", owner);
            ImGui::TextUnformatted(buf);
            break;
        }
    }
}

}  // namespace lw::ui
