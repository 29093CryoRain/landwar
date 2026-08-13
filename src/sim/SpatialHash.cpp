#include "sim/SpatialHash.h"

#include <algorithm>
#include <cmath>

#include "sim/components.h"

namespace lw {

void SpatialHash::build(entt::registry& reg, int width, int height) {
    width_ = width;
    height_ = height;
    // 桶数组只按尺寸变化时重排一次；每轮只清空上一轮触及过的桶（touched_），
    // 避免每 tick 重建 9975 个空 vector（Phase 5 浸泡测试定位到的热点）。
    const size_t cellCount = static_cast<size_t>(width) * height;
    if (buckets_.size() != cellCount) {
        buckets_.clear();
        buckets_.resize(cellCount);
    }
    for (const size_t idx : touched_) buckets_[idx].clear();
    touched_.clear();

    auto view = reg.view<comp::Position, comp::Collider>();
    // P9 确定性：按实体 id 降序迭代（等价无回收时视图逆序）。create(hint) 按 index 排序
    // packed → 快照读档后的存储序与直跑不可复现，但 id 降序与存储序无关 → 候选顺序、
    // 碰撞"首个命中"与直跑逐位一致（回放/读档成立的前提）。
    std::vector<entt::entity> ents(view.begin(), view.end());
    std::sort(ents.begin(), ents.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) > entt::to_integral(b);
    });
    for (auto e : ents) {
        if (reg.all_of<comp::Dead>(e)) continue;
        const auto& pos = view.get<comp::Position>(e);
        // 位置可夹到 [0, width]/[0, height]（移动末尾 clamp），落到最近的格桶。
        const int cx = std::clamp(static_cast<int>(pos.x), 0, width_ - 1);
        const int cy = std::clamp(static_cast<int>(pos.y), 0, height_ - 1);
        const size_t idx = static_cast<size_t>(cy) * width_ + cx;
        if (buckets_[idx].empty()) touched_.push_back(idx);  // 本轮首次触达才记录
        buckets_[idx].push_back(e);
    }
}

// reg 仅用于 queryCircle 的精确过滤；queryAABB 只查桶，不需要 reg（保留签名对称）。
std::vector<entt::entity> SpatialHash::queryAABB([[maybe_unused]] const entt::registry& reg,
                                                 double x0, double y0, double x1, double y1) const {
    std::vector<entt::entity> result;
    if (buckets_.empty()) return result;
    const int cx0 = std::clamp(static_cast<int>(std::floor(x0)), 0, width_ - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor(x1)), 0, width_ - 1);
    const int cy0 = std::clamp(static_cast<int>(std::floor(y0)), 0, height_ - 1);
    const int cy1 = std::clamp(static_cast<int>(std::floor(y1)), 0, height_ - 1);
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const auto& bucket = buckets_[static_cast<size_t>(cy) * width_ + cx];
            result.insert(result.end(), bucket.begin(), bucket.end());
        }
    }
    return result;
}

std::vector<entt::entity> SpatialHash::queryCircle(const entt::registry& reg, double x, double y,
                                                   double radius) const {
    std::vector<entt::entity> result;
    if (buckets_.empty()) return result;
    const int x0 = std::max(0, static_cast<int>(std::floor(x - radius)));
    const int x1 = std::min(width_ - 1, static_cast<int>(std::floor(x + radius)));
    const int y0 = std::max(0, static_cast<int>(std::floor(y - radius)));
    const int y1 = std::min(height_ - 1, static_cast<int>(std::floor(y + radius)));
    const double r2 = radius * radius;
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            for (auto e : buckets_[static_cast<size_t>(cy) * width_ + cx]) {
                const auto& pos = reg.get<comp::Position>(e);
                const double dx = pos.x - x;
                const double dy = pos.y - y;
                if (dx * dx + dy * dy < r2) result.push_back(e);
            }
        }
    }
    return result;
}

}  // namespace lw
