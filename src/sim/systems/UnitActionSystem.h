// UnitActionSystem.h — 兵周期动作（开发计划 P9 任务 0）。
// 对 periodic 非 none 的兵推进 Behavior.counter，到 periodTicks 触发动作（P9=发射子弹）。
#pragma once

namespace lw {

class Simulation;

class UnitActionSystem {
public:
    // 推进全部兵的行为计数并触发周期动作。霰弹发射消耗 RNG（每颗 2 次），手枪 0 次。
    static void update(Simulation& sim);
};

}  // namespace lw
