// EffectSystem.h — 特效系统（爆炸/地雷/激光，原版 Effect::solve0/1/2，翻新计划 §2.6）。
// 在军队阶段之后求解（原版 Solve_effect）：特效创建当 tick 即以 tt=0 参与求解。
// 消亡的特效在本阶段末统一销毁（不影响正在迭代的特效列表）。
#pragma once

namespace lw {

class Simulation;

class EffectSystem {
public:
    // 每 tick 求解全部特效；销毁自身消亡的特效（爆炸 tt>8、地雷引爆/超时、激光超龄）。
    static void update(Simulation& sim);
};

}  // namespace lw
