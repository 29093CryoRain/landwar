// MessageLog.h — 消息存储与上限逻辑（开发计划 P4 修正）。
// 纯逻辑、可单测：消息追加 + 超上限丢最旧。**消息不过期/不自动消失**（P4 修正：取消
// ttl 渐隐设计，无限留存，仅消息过多时立即丢弃最旧，思路 6.0.5）。
// 渲染（MessagePanel）只读本结构；不依赖 ImGui/平台 → 测试可直接 include。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lw::ui {

struct Message {
    std::string text;        // 完整文案（含事件时间前缀与势力名，如 "(333s) 势力 红 被灭亡"）
    std::uint64_t tick = 0;  // 事件发生时间（tick；显示用，P4 修正）
    int factionId = 0;       // 相关势力（渲染时仅势力名按此配色；0=中立灰）
};

class MessageLog {
public:
    // 追加一条消息（text 移动；不过期，持续留存）。
    void add(std::string text, std::uint64_t tick, int factionId) {
        messages_.push_back(Message{std::move(text), tick, factionId});
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
