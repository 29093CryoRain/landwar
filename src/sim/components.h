// components.h — entt ECS 组件定义（翻新计划 §3.3）。
// 放 lw::comp 命名空间，避免与 lw::FactionId 类型别名冲突。
#pragma once

#include <entt/entt.hpp>

#include "core/GameDefs.h"

namespace lw::comp {

// ---- 兵组件（§3.3）----
struct Position { double x = 0, y = 0; };
struct Velocity { double angle = 0; };         // 世界方向角
struct Speed { double value = 0; };            // 本 tick 移动量（下海 /2、登陆 ×2，原版 speed）
struct OnLand { bool value = true; };          // 陆上(1)/海上(0)
// 山地状态（P5）：山是陆地子集 → onLand=true 且 inMountain=true 才在山里。
// speed 语义 = 当前速度（山地内已 ×mountainSpeedMult；进出山互逆缩放，见 MovementSystem）。
struct MountainState { bool inMountain = false; };
struct FactionId { int value = 0; };
struct UnitType { ArmyType type = ArmyType::normal; };
struct Collider { double radius = 1.0; };
struct LandHistory { int lastLandTime = 0; };  // 上次登陆时间（lst_move_to_land_time）
struct Dead {};                                // 待销毁标记（tick 末由 DeathSystem 统一处理）

// 行为组件（P9 行为抽象）：死亡特效 + 周期动作。静态部分（deathEffect/periodic/periodTicks）
// 来自 Config::Unit，spawnArmy 时填入；counter 为运行时计数（快照序列化）。
struct Behavior {
    DeathEffect deathEffect = DeathEffect::none;
    PeriodicAction periodic = PeriodicAction::none;
    int periodTicks = 0;  // 动作间隔（tick）
    int counter = 0;      // 距上次动作计数（UnitActionSystem 递增）
};

// 射弹（P9）：子弹实体。**标志组件**（兵无此组件，视图/空间查询用它区分子弹与兵）。
// 复用 Position/Velocity/Speed/FactionId/UnitType(发射兵类型)/Collider(子弹半径)。
struct Projectile {
    int lifespanTicks = 0;   // 剩余寿命（tick，每 tick 消耗 1；山地额外扣 penalty，归零销毁）
    bool inMountain = false; // 当前所在格是否为山（山地留存惩罚；陆→山 撞山消失）
};

// ---- 特效组件（Phase 4 求解；Phase 3 仅创建）----
struct EffectTypeId { EffectType type = EffectType::bomb; };
struct EffectTimer { int createdTick = 0; };   // create_time
// 原版 lsdouble1/2/3 通用参数槽（含义随类型不同，见翻新计划 §2.6）。
struct EffectParams { double p0 = 0, p1 = 0, p2 = 0; };
// 创建者快照（原版 creator_id：死兵位置不随 tick 变化，快照避免悬垂实体引用）。
// P11：unitType = 创建者兵种（击杀/占领 credit 链：特效/子弹击杀 credit 回创建者 (faction, type)）。
struct Creator { double x = 0, y = 0; int factionId = 0; int unitType = 0; };

}  // namespace lw::comp

namespace lw {

// 死亡事件（战斗/爆炸/激光击杀时记录；tick 末 DeathSystem 生成死亡效果并销毁实体）。
struct DeathEvent {
    entt::entity entity = entt::null;
    ArmyType type = ArmyType::normal;
    int factionId = 0;
    double x = 0, y = 0;        // 死亡位置
    double killerX = 0, killerY = 0;  // 击杀者位置（激光死亡效果角度用）
};

}  // namespace lw
