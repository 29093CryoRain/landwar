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
// 过河停顿结转（二期反馈）：remLength 是每 tick 的局部量，跨 tick 不保留。河骰通过且真正进入
// 目标格时，本 tick 剩余步长归零、未用完的剩余量结转到下一 tick（下一 tick 起始 = 本值，不补满），
// 于是两 tick 位移合计恰好一份 speed（净停 1 tick）。0 = 无结转。
struct MoveCarry { double value = 0.0; };
struct Dead {};                                // 待销毁标记（tick 末由 DeathSystem 统一处理）
// 盟友边界闸门（.docs/包围检测.md / 兵运动与盟友系统开发文档.md §12）：每兵一份 CUSUM 统计量，
// 观测 = 该兵在盟友领土边界上的**反弹**次数（河/山骰反弹与盟友规则反弹一并计入；直接穿过不计），
// 泄漏按**本 tick 步长**归一（结合该兵速度），故各速度下判据一致。
// cusum >= config.allyGate.alarmThreshold 时"闸门打开"：下一次穿越盟友边界放行一次并清零，
// 避免小飞地被盟友领土围死导致单位永久滞留。新兵为 0；随快照保存（影响模拟分支）。
struct AllyGate { double cusum = 0.0; };

// 行为组件（P9 行为抽象）：死亡特效 + 周期动作。静态部分（deathEffect/periodic/periodTicks）
// 来自 Config::Unit，spawnArmy 时填入；counter 为运行时计数（快照序列化）。
struct Behavior {
    DeathEffect deathEffect = DeathEffect::none;
    PeriodicAction periodic = PeriodicAction::none;
    int periodTicks = 0;  // 动作间隔（tick）
    int counter = 0;      // 距上次动作计数（UnitActionSystem 递增）
};

// Combat entity taxonomy:
// - Every projectile or non-projectile combat effect created by SpawnSystem carries CombatEntity.
// - CombatEffect is the non-projectile subtype; Projectile remains a separate data component.
// - Future combat entities belong in this section and must carry CombatEntity plus their own
//   subtype marker/data; give each solver its own system rather than folding unlike solvers together.
struct CombatEntity {};
struct CombatEffect {};

// 射弹（P9）：子弹实体。**标志组件**（兵无此组件，视图/空间查询用它区分子弹与兵）。
// 复用 Position/Velocity/Speed/FactionId/UnitType(发射兵类型)/Collider(子弹半径)。
struct Projectile {
    int lifespanTicks = 0;   // 剩余寿命（tick，每 tick 消耗 1；山地额外扣 penalty，归零销毁）
    bool inMountain = false; // 当前所在格是否为山（山地留存惩罚；陆→山 撞山消失）
};

// ---- 特效组件（Phase 4 求解；Phase 3 仅创建）----
struct CombatEffectTypeId { CombatEffectType type = CombatEffectType::bomb; };
struct CombatEffectTimer { int createdTick = 0; };   // create_time
// 原版 lsdouble1/2/3 通用参数槽（含义随类型不同，见翻新计划 §2.6）。
struct CombatEffectParams { double p0 = 0, p1 = 0, p2 = 0; };
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
