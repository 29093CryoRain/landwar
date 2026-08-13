// ProjectileSystem.h — 射弹求解（开发计划 P9 任务 3）。
// 子弹每 tick 沿角推进（复用 findNextXY 逐格），穿海陆不减速；占领途经领地；击杀敌兵；
// 不能从陆进山（撞山消失）；在山地留存变短；超时消失。
#pragma once

namespace lw {

class Simulation;

class ProjectileSystem {
public:
    // 移动全部子弹并处理命中/占领/超时。移动本身不消耗 RNG（角度发射时已定）。
    static void update(Simulation& sim);
};

}  // namespace lw
