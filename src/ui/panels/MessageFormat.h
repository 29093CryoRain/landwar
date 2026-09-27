// MessageFormat.h — 结构化游戏事件 → 消息文本段（UI 层组装）。
// 依据 .docs/工程规范.md「展示文本不进模拟核心」：Simulation 只发 GameEvent（kind + 结构化字段），
// 中文文案、事件时间前缀、势力名分段全部在这里组装。
// 势力名以 MessageSpan::factionId 表达 → 渲染端按 id 画名字，**不做字符串匹配**。
// 纯逻辑（不依赖 ImGui）→ 测试可直接 include 并断言段结构。
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/GameDefs.h"  // formatEventTime
#include "core/Simulation.h"
#include "ui/panels/MessageLog.h"

namespace lw::ui {

inline std::vector<MessageSpan> formatGameEvent(const Simulation& sim, const GameEvent& ev) {
    std::vector<MessageSpan> spans;
    const auto literal = [&spans](std::string text) {
        if (!text.empty()) spans.push_back({std::move(text), -1});
    };
    const auto name = [&spans](int factionId) { spans.push_back({std::string{}, factionId}); };
    const std::string prefix = formatEventTime(ev.tick, sim.config().sim.tickRate);

    switch (ev.kind) {
        case GameEventKind::FactionAnnihilated:
            literal(prefix + " 势力 ");
            name(ev.factionId);
            literal(" 被灭亡");
            break;
        case GameEventKind::Unification:
            literal(prefix + " ");
            if (ev.factionIds.size() > 1) {
                // 联盟共同统一：所有存活成员名各按自己的势力色（factionIds 顺序 = 确定性）。
                literal("联盟 " + std::to_string(ev.data + 1) + "（");
                for (std::size_t i = 0; i < ev.factionIds.size(); ++i) {
                    if (i > 0) literal("、");
                    name(ev.factionIds[i]);
                }
                literal("）统一天下");
            } else {
                literal("势力 ");
                name(ev.factionId);
                literal(" 统一天下");
            }
            break;
        case GameEventKind::TechAcquired: {
            literal(prefix + " 势力 ");
            name(ev.factionId);
            const auto& techs = sim.config().tech.techs;
            const bool valid = ev.data >= 0 && ev.data < static_cast<int>(techs.size());
            literal(" 获得科技 " + (valid ? techs[static_cast<std::size_t>(ev.data)].name : "?")
                    + "(" + std::to_string(ev.level) + "级)");
            break;
        }
        case GameEventKind::CapitalLost:
            literal(prefix + " 势力 ");
            name(ev.factionId);
            literal(" 的首都被攻破");
            break;
        case GameEventKind::Custom:
        default:
            // 自定义事件没有结构可组装：literal 即完整文案（调用方自带时间前缀等）。
            literal(ev.literal);
            break;
    }
    return spans;
}

}  // namespace lw::ui
