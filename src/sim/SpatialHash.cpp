#include "sim/SpatialHash.h"

#include <algorithm>
#include <cmath>

#include "sim/components.h"

namespace lw {

// 位置 → 桶下标（界外/贴界 → 内缩兜底）。方形 = floor（原语义）；六/三 = worldToCell。
int SpatialHash::bucketIndex(double x, double y) const {
    if (geom_.type == TilingType::Square) {
        const int cx = std::clamp(static_cast<int>(x), 0, geom_.cols - 1);
        const int cy = std::clamp(static_cast<int>(y), 0, geom_.rows - 1);
        return cy * geom_.cols + cx;
    }
    int idx = geom_.worldToCell(x, y);
    if (idx < 0) {  // 贴界（如 y = worldHeight）：内缩 ε 再取
        idx = geom_.worldToCell(std::clamp(x, 0.0, geom_.worldWidth() - kEps),
                                std::clamp(y, 0.0, geom_.worldHeight() - kEps));
    }
    return std::max(0, idx);
}

void SpatialHash::build(entt::registry& reg, const TilingGeom& g) {
    geom_ = g;
    const size_t cellCount = static_cast<size_t>(g.cellCount());
    if (buckets_.size() != cellCount) {
        buckets_.clear();
        buckets_.resize(cellCount);
    }
    // 防御：touched_ 可能是上一轮不同几何（如方/密铺切换）留下的越界下标 → 先按新尺寸过滤。
    for (const size_t idx : touched_)
        if (idx < buckets_.size()) buckets_[idx].clear();
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
        const size_t idx = static_cast<size_t>(bucketIndex(pos.x, pos.y));
        if (buckets_[idx].empty()) touched_.push_back(idx);  // 本轮首次触达才记录
        buckets_[idx].push_back(e);
    }
}

void SpatialHash::build(entt::registry& reg, int width, int height) {
    build(reg, TilingGeom{TilingType::Square, width, height});
}

// reg 仅用于 queryCircle 的精确过滤；queryAABB 只查桶，不需要 reg（保留签名对称）。
std::vector<entt::entity> SpatialHash::queryAABB([[maybe_unused]] const entt::registry& reg,
                                                 double x0, double y0, double x1, double y1) const {
    std::vector<entt::entity> result;
    if (buckets_.empty()) return result;
    int r0, r1, c0, c1;
    geom_.rowRange(y0, y1, r0, r1);
    if (r0 > r1) return result;
    for (int r = r0; r <= r1; ++r) {
        geom_.colRange(x0, x1, r, c0, c1);
        if (c0 > c1) continue;
        for (int c = c0; c <= c1; ++c) {
            const int B = geom_.baseCount();
            for (int b = 0; b < B; ++b) {
                const int idx = geom_.cellIndexAt(r, c, b);
                if (idx < 0) continue;
                const auto& bucket = buckets_[static_cast<size_t>(idx)];
                result.insert(result.end(), bucket.begin(), bucket.end());
            }
        }
    }
    return result;
}

std::vector<entt::entity> SpatialHash::queryCircle(const entt::registry& reg, double x, double y,
                                                   double radius) const {
    std::vector<entt::entity> result;
    if (buckets_.empty()) return result;
    int r0, r1, c0, c1;
    geom_.rowRange(y - radius, y + radius, r0, r1);
    if (r0 > r1) return result;
    const double r2 = radius * radius;
    for (int r = r0; r <= r1; ++r) {
        geom_.colRange(x - radius, x + radius, r, c0, c1);
        if (c0 > c1) continue;
        for (int c = c0; c <= c1; ++c) {
            const int B = geom_.baseCount();
            for (int b = 0; b < B; ++b) {
                const int idx = geom_.cellIndexAt(r, c, b);
                if (idx < 0) continue;
                for (auto e : buckets_[static_cast<size_t>(idx)]) {
                    const auto& pos = reg.get<comp::Position>(e);
                    const double dx = pos.x - x;
                    const double dy = pos.y - y;
                    if (dx * dx + dy * dy < r2) result.push_back(e);
                }
            }
        }
    }
    return result;
}

}  // namespace lw
