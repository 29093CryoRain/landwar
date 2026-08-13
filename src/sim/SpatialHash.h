// SpatialHash.h — 以地图格为桶的空间哈希（翻新计划 §3.3）。
// 每 tick 重建一次（军队阶段与特效阶段各一次）；战斗/爆炸/地雷用 queryCircle，
// 激光用 queryAABB（包围盒候选 + pointDistanceFromSegment 精确过滤）。
#pragma once

#include <entt/entt.hpp>

#include <vector>

namespace lw {

class SpatialHash {
public:
    // 重建桶索引：桶内为「有 Position+Collider 且非 Dead」的实体（即活兵）。
    void build(entt::registry& reg, int width, int height);

    // 圆查询：返回与 (x,y) 距离 < radius 的候选实体（精确过滤）。
    std::vector<entt::entity> queryCircle(const entt::registry& reg, double x, double y,
                                          double radius) const;

    // 包围盒查询：返回 AABB [x0,x1]×[y0,y1] 覆盖格内的全部候选实体（不做距离过滤；
    // 供激光按 pointDistanceFromSegment 精确过滤用，避免全量 O(n) 扫描）。
    std::vector<entt::entity> queryAABB(const entt::registry& reg, double x0, double y0,
                                        double x1, double y1) const;

private:
    std::vector<std::vector<entt::entity>> buckets_;  // index = cy*width_ + cx
    std::vector<std::size_t> touched_;  // 本轮触及过的桶下标（build 时只清这些，避免全量重建）
    int width_ = 0;
    int height_ = 0;
};

}  // namespace lw
