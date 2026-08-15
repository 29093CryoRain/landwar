// GameDefs.h — 核心类型与常量定义（翻新计划 §3.1 core/GameDefs.h）。
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace lw {

// 兵种类型（原版 type 值，见翻新计划 §2.2）。P9：新增手枪/霰弹（射弹兵）。
enum class ArmyType : int {
    normal = 0,    // 普通
    vanguard = 1,  // 先锋
    pioneer = 2,   // 开拓
    laser = 3,     // 激光
    bomb = 4,      // 爆炸
    mine = 5,      // 地雷
    pistol = 6,    // 手枪（P9 射弹兵）
    shotgun = 7,   // 霰弹（P9 射弹兵）
    count = 8,
};

// 死亡特效（P9 行为抽象：DeathSystem 按此生成，替代硬编码 switch(type)）。
enum class DeathEffect : int {
    none = 0,  // 无特效
    laser = 1, // 激光（死亡光束）
    bomb = 2,  // 爆炸
    mine = 3,  // 地雷
};

// 周期动作（P9 行为抽象：UnitActionSystem 按此周期性触发）。
enum class PeriodicAction : int {
    none = 0,        // 无周期动作
    firePistol = 1,  // 手枪：向正前方发 1 颗恒定速度子弹
    fireShotgun = 2, // 霰弹：向随机方向发 3 颗随机速度子弹
};

// 特效类型（原版 Effect.type 值，见翻新计划 §2.6）。
enum class EffectType : int {
    bomb = 0,  // 爆炸
    mine = 1,  // 地雷（布设状态）
    laser = 2, // 激光
};

// 增益类型（P7 纯数值类；P8 科技=追加 source="tech:<id>" 的 buff）。
// 原定义于 sim/Buff.h，迁入本公共头：Config（tech 表）需引用 BuffType，且 Config↔Buff 互相包含，
// 放公共头打破循环。param 语义见各注释；magnitude 语义随类型（乘数/条数）。
// 聚合规则（思路「科技细节」：数值增加默认加算、数值减小默认乘算）：见 sim/Buff.cpp computeMods。
enum class BuffType : int {
    UnitCostMult,        // param=-1 全兵 | 兵种下标；造价乘数（<1 便宜；乘算）
    UnitSpeedMult,       // param=-1 全兵 | 兵种下标；速度乘数（>1 快；加算）
    SeaChanceMult,       // 下海概率乘数（<1 少下海；乘算）
    BounceChanceMult,    // 碰敌反弹概率乘数（<1 少反弹；乘算）
    EconomyGainMult,     // 经济产出乘数（>1 多；加算）
    BombRadiusMult,      // 爆炸半径乘数（×势力定义基数；加算）
    LaserDurationMult,   // 激光寿命乘数（加算）
    LaserLengthMult,     // 激光长度乘数（加算）
    LaserWidthMult,      // 激光击杀宽度乘数（变宽；加算）
    LaserExtraBeams,     // 额外激光条数（加算）
    MineTimeoutMult,     // 地雷超时乘数（延长；加算）
    FreeArmyChanceMult,  // 攻占城市免费兵概率乘数（乘算）
    // P8 科技新增：
    UnitActionRateMult,  // param=兵种；周期动作（射速）乘数（>1 快；加算）
    ProjectileCountExtra,// param=兵种；每次齐射额外子弹数（条数，加和）
    TechGainMult,        // 科技点产出乘数（>1 多；加算）
};

using FactionId = int;

// 势力数（含中立势力 0）与可玩势力数。
inline constexpr int kFactionTotal = 9;      // 0..8，0 为中立灰
inline constexpr int kPlayerFactionCount = 8; // 1..8
inline constexpr FactionId kNeutralFaction = 0;

// 兵种数量（数组维度用，= ArmyType::count）。
inline constexpr int kArmyTypeCount = static_cast<int>(ArmyType::count);

// 数学常量（原版 Define.h 的 PI / eps）。
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kEps = 0.000001;

// 密铺类型（P12 异种地图；类型定义放公共头，几何在 world/tiling/Tiling.h）。
// 2026-08-16 起扩展：7 种半正（阿基米德）密铺 + 7 种 Laves 密铺（对偶）。
// 不做 3.3.3.4.4 及其 Laves 对偶五边形，不为其预留编号。
enum class TilingType : int {
    Square = 0,
    Hex = 1,  // 正六边形（尖顶：顶点在正上下方）
    Tri = 2,  // 正三角形（正向/反向两种朝向）
    // ---- 半正（阿基米德）密铺 ----
    Arch33336 = 3,  // 3.3.3.3.6（三角形:六边形 = 8:1）
    Arch33434 = 4,  // 3.3.4.3.4（三角形:正方形 = 2:1）
    Arch3464 = 5,   // 3.4.6.4（三角形:正方形:六边形 = 2:3:1）
    Arch3636 = 6,   // 3.6.3.6（三角形:六边形 = 2:1）
    Arch31212 = 7,  // 3.12.12（三角形:十二边形 = 2:1）
    Arch4612 = 8,   // 4.6.12（正方形:六边形:十二边形 = 3:2:1）
    Arch488 = 9,    // 4.8.8（正方形:八边形 = 1:1）
    // ---- Laves 密铺（半正密铺的对偶，单种凸多边形面）----
    Laves3636 = 10,  // 对偶 3.6.3.6：菱形
    Laves31212 = 11, // 对偶 3.12.12：等腰三角形
    Laves4612 = 12,  // 对偶 4.6.12：不等边三角形
    Laves488 = 13,   // 对偶 4.8.8：等腰直角三角形
    Laves33434 = 14, // 对偶 3.3.4.3.4：五边形
    Laves33336 = 15, // 对偶 3.3.3.3.6：五边形
    Laves3464 = 16,  // 对偶 3.4.6.4：筝形（四边形）
};

inline constexpr int kTilingTypeCount = 17;

inline const char* tilingName(TilingType t) {
    switch (t) {
        case TilingType::Square: return "square";
        case TilingType::Hex: return "hex";
        case TilingType::Tri: return "tri";
        case TilingType::Arch33336: return "arch_33336";
        case TilingType::Arch33434: return "arch_33434";
        case TilingType::Arch3464: return "arch_3464";
        case TilingType::Arch3636: return "arch_3636";
        case TilingType::Arch31212: return "arch_31212";
        case TilingType::Arch4612: return "arch_4612";
        case TilingType::Arch488: return "arch_488";
        case TilingType::Laves3636: return "laves_3636";
        case TilingType::Laves31212: return "laves_31212";
        case TilingType::Laves4612: return "laves_4612";
        case TilingType::Laves488: return "laves_488";
        case TilingType::Laves33434: return "laves_33434";
        case TilingType::Laves33336: return "laves_33336";
        case TilingType::Laves3464: return "laves_3464";
    }
    return "square";
}

inline TilingType tilingFromName(const std::string& name) {
    if (name == "hex") return TilingType::Hex;
    if (name == "tri") return TilingType::Tri;
    if (name == "arch_33336") return TilingType::Arch33336;
    if (name == "arch_33434") return TilingType::Arch33434;
    if (name == "arch_3464") return TilingType::Arch3464;
    if (name == "arch_3636") return TilingType::Arch3636;
    if (name == "arch_31212") return TilingType::Arch31212;
    if (name == "arch_4612") return TilingType::Arch4612;
    if (name == "arch_488") return TilingType::Arch488;
    if (name == "laves_3636") return TilingType::Laves3636;
    if (name == "laves_31212") return TilingType::Laves31212;
    if (name == "laves_4612") return TilingType::Laves4612;
    if (name == "laves_488") return TilingType::Laves488;
    if (name == "laves_33434") return TilingType::Laves33434;
    if (name == "laves_33336") return TilingType::Laves33336;
    if (name == "laves_3464") return TilingType::Laves3464;
    return TilingType::Square;
}

// 签名兵种：该兵种使用"特殊版"贴图的势力 id（0=无特殊版）。army_gray.png 第 1 列存特殊版。
// 目前仅下列势力特色兵种有特殊版（青先锋/蓝开拓/绿激光/橙爆炸/紫地雷）；P8 科技后可作为高级兵。
// P9 手枪/霰弹暂无特殊版（占位形状渲染，不走 sprite 行）。
inline constexpr int kSpecialUnitFaction[kArmyTypeCount] = {
    /*normal*/ 0, /*vanguard(青3)*/ 3, /*pioneer(蓝4)*/ 4,
    /*laser(绿5)*/ 5, /*bomb(橙6)*/ 6, /*mine(紫7)*/ 7,
    /*pistol*/ 0, /*shotgun*/ 0,
};

// 势力名单字显示名（红/黄/青/蓝/绿/橙/紫/品；0/越界=中立"?"）。
// 与 UI（Menu/DebugPanel）共用一份；P4 消息文案复用（灭亡/统一事件）。
inline const char* factionShortName(int id) {
    switch (id) {
        case 1: return "红";
        case 2: return "黄";
        case 3: return "青";
        case 4: return "蓝";
        case 5: return "绿";
        case 6: return "橙";
        case 7: return "紫";
        case 8: return "品";
        default: return "?";
    }
}

// 事件时间格式（P4 修正：消息显示事件发生时间，用户可读的秒，不显示 debug 风格变量名）：
// tick → "(Xs)"（60tick/s → 20000 tick ≈ 333s）。Simulation/事件系统共用一份。
inline std::string formatEventTime(std::uint64_t tick, double tickRate) {
    const int secs = static_cast<int>(static_cast<double>(tick) / (tickRate > 0 ? tickRate : 60.0));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "(%ds)", secs);
    return std::string(buf);
}

}  // namespace lw
