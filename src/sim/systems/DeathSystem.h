// DeathSystem.h — 死亡处理（翻新计划 §2.5 / Phase 3）。
// tick 末统一销毁标死实体并生成死亡效果（避免遍历中销毁）。
#pragma once

#include <entt/entt.hpp>

namespace lw {

class Simulation;

class DeathSystem {
public:
    // 处理本 tick 全部死亡：生成死亡效果（§2.5）并销毁实体。
    static void flush(Simulation& sim);
};

}  // namespace lw
