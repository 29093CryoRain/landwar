// Buff.h — 增益系统（开发计划 P7，思路 6.1）。
// 增益 = 数据实例（可序列化），行为 = 系统经 FactionMods 聚合面读取（不 switch 具体 buff）。
// 势力定义的分工（迁移原则）：
//   - 【定义与加成】armyCost（= baseCost，P11 改版后各势力同价）仍为基础定义；
//   - factions.json 中所有可叠加的势力特性统一迁移为初始 buff（source="faction"），
//     与 tech.levels 共用 type/param/magnitude 格式。
//   P8 科技 = 追加 source="tech:<id>" 的 buff；机制类增益（特性转移/行为钩子）待扩展。
// 确定性：computeMods 纯函数、无 RNG；聚合顺序固定 → 同 buffs 必得同 mods。
// 位精确：迁移后各系统读取形如 `x * mods.m`，默认 m=1.0（IEEE ×1.0 恒等）→ 基线不变。
#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/GameDefs.h"

namespace lw {

// 一条增益：数据实例。source 供 UI 溯源（"faction" / "tech:<id>"）。
struct Buff {
    BuffType type = BuffType::UnitSpeedAdd;
    int param = -1;          // 兵种下标（-1 = 全兵种）
    double magnitude = 0.0;  // Add 类型为增量；Mult 类型为乘数；Extra 类型为条数
    int stacks = 1;          // 同 type 合并条数（聚合按 stacks 次应用）
    std::string source;      // 来源
};

// 系统读取面：某势力全部 buff 聚合成的数值集。系统只读它。
// 聚合规则（P8，思路"科技细节"：数值增加默认加算、数值减小默认乘算）：
//   - 加算型（增幅）：result = 1 + Σm——速度/经济/半径/寿命/长度/宽度/地雷超时/射速/科技点；
//   - 乘算型（减幅/概率乘数）：result = Πm——造价/下海/反弹/免费兵；
//   - 条数：result = Σm——额外激光/额外子弹。
// 迁移保护：各势力初始每组恰一条 buff（单条时 1+(m−1)==m）→ 基线不变。
struct FactionMods {
    std::array<double, kArmyTypeCount> costMult{};    // 造价乘数（乘算）
    std::array<double, kArmyTypeCount> speedMult{};   // 速度乘数（加算）
    std::array<double, kArmyTypeCount> actionRateMult{};  // 周期动作（射速）乘数（P8 高速射击；加算）
    std::array<int,    kArmyTypeCount> projectileCountExtra{};  // 额外子弹数（P8 弹幕；条数加和）
    double seaChanceMult = 1.0;      // 下海概率乘数
    double bounceChanceMult = 1.0;   // 碰敌反弹概率乘数
    double economyGainMult = 1.0;    // 经济产出乘数
    double explosionRadiusAdd = 1.0; // 通用爆炸效果半径增幅
    double bombExplosionRadiusAdd = 1.0; // 爆炸兵死亡爆炸半径增幅
    double laserDurationMult = 1.0;  // 激光寿命乘数
    double laserLengthMult = 1.0;    // 激光长度乘数
    double laserWidthMult = 1.0;     // 激光击杀宽度乘数
    int    laserExtraBeams = 0;      // 额外激光条数（加算）
    double mineTimeoutMult = 1.0;    // 地雷超时乘数
    double freeArmyChanceMult = 1.0; // 攻占城市免费兵概率乘数
    double freeArmyChance = 0.0;     // 攻占城市免费兵基础概率
    int    freeArmyType = static_cast<int>(ArmyType::normal); // 免费产出的兵种
    double mineExplosionRadiusAdd = 1.0; // 地雷产生的爆炸半径增幅
    double techGainMult = 1.0;       // 科技点产出乘数（P8 观星台；加算）

    FactionMods() {
        costMult.fill(1.0);
        speedMult.fill(1.0);
        actionRateMult.fill(1.0);
        projectileCountExtra.fill(0);
    }
};

// 聚合：buffs → FactionMods。纯函数、无 RNG。见头注释聚合规则。
FactionMods computeMods(const std::vector<Buff>& buffs);

// P8：把势力科技等级表（与 config.techs 对齐）翻译为 buff 列表（source="tech:<id>"）。
// 只含 level>0 的科技，每条取该科技当前级的累计效果。纯函数、无 RNG。
std::vector<Buff> techBuffs(const Config& cfg, const std::vector<int>& levels);

// 聚合类别（思路"科技细节"：数值增加默认加算、数值减小默认乘算）。
enum class BuffKind { Multiplicative, Additive, Count };
BuffKind buffKind(BuffType t);

// 数值文案（科研弹窗/科技面板用）：加算按 m×100、乘算按 (m-1)×100 显示，条数 "+N"。
std::string buffValueText(BuffType type, double magnitude);

// 势力定义的初始 buff（source="faction"）：把 Config::Faction 修饰符字段翻译为 buff。
// 顺序：全兵修饰符在前、特定兵种在后（与迁移前速度链顺序一致）。
std::vector<Buff> initialFactionBuffs(const Config::Faction& def);

}  // namespace lw
