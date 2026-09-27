// MessageLog.h — 消息存储与上限逻辑（开发计划 P4 修正）。
// 纯逻辑、可单测：消息追加 + 超上限丢最旧。**消息不过期/不自动消失**（P4 修正：取消
// ttl 渐隐设计，无限留存，仅消息过多时立即丢弃最旧，思路 6.0.5）。
// 渲染（MessagePanel）只读本结构；不依赖 ImGui/平台 → 测试可直接 include。
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lw::ui {

// 消息文本的一段：
//   factionId >= 0 → 该位置是"势力名"，渲染端按 Config::Faction.nameColors 逐字着色；
//   factionId < 0  → 该位置是字面文本（默认白色）。
// 势力名以 **id** 表达而不是字符串 → 渲染端不需要、也不允许在文案里 find 名字
// （旧实现"文本 + 单个 factionId + text.find(名字)"已删除）。
struct MessageSpan {
    std::string literal;
    int factionId = -1;
};

struct Message {
    // 结构化文本段（文案与势力名分段在 UI 层组装：ui::formatGameEvent）。
    std::vector<MessageSpan> spans;
    std::uint64_t tick = 0;  // 事件发生时间（tick；显示用，P4 修正）
};

class MessageLog {
public:
    // 追加一条消息（段表移动；不过期，持续留存）。
    void add(std::vector<MessageSpan> spans, std::uint64_t tick) {
        messages_.push_back(Message{std::move(spans), tick});
    }

    // 仅按上限丢弃最旧（消息过多时立即消失；无时间过期）。
    void prune(int maxShown) {
        const std::size_t cap = maxShown > 0 ? static_cast<std::size_t>(maxShown) : 0;
        if (messages_.size() > cap)
            messages_.erase(messages_.begin(), messages_.begin() + (messages_.size() - cap));
    }

    const std::vector<Message>& messages() const { return messages_; }
    bool empty() const { return messages_.empty(); }
    void clear() { messages_.clear(); }

private:
    std::vector<Message> messages_;
};

}  // namespace lw::ui
