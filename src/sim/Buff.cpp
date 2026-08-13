// Buff.cpp — 增益聚合实现（开发计划 P7 + P8 科技）。
#include "sim/Buff.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace lw {

namespace {

// 聚合类别（P8，思路"科技细节"：数值增加默认加算、数值减小默认乘算）。
BuffKind kindOf(BuffType t) {
    switch (t) {
        case BuffType::UnitCostMult:
        case BuffType::SeaChanceMult:
        case BuffType::BounceChanceMult:
        case BuffType::FreeArmyChanceMult:
            return BuffKind::Multiplicative;  // 减幅/概率乘数：连乘
        case BuffType::LaserExtraBeams:
        case BuffType::ProjectileCountExtra:
            return BuffKind::Count;           // 条数：加和
        default:
            return BuffKind::Additive;        // 增幅：1 + Σ(m-1)
    }
}

// 对数组型增益按 param 应用（param<0 → 全兵；否则仅该兵种）。
template <typename Fn>
void forParam(int param, Fn&& fn) {
    if (param < 0) {
        for (int t = 0; t < kArmyTypeCount; ++t) fn(t);
    } else {
        fn(param);
    }
}

}  // namespace

BuffKind buffKind(BuffType t) { return kindOf(t); }

FactionMods computeMods(const std::vector<Buff>& buffs) {
    FactionMods m;  // 默认初始化即各恒等（加算 1.0、乘算 1.0、条数 0）
    for (const Buff& b : buffs) {
        const int n = std::max(1, b.stacks);
        for (int s = 0; s < n; ++s) {
            switch (b.type) {
                case BuffType::UnitCostMult:  // 乘算
                    forParam(b.param, [&](int t) {
                        m.costMult[static_cast<size_t>(t)] *= b.magnitude;
                    });
                    break;
                case BuffType::UnitSpeedMult:  // 加算
                    forParam(b.param, [&](int t) {
                        m.speedMult[static_cast<size_t>(t)] += b.magnitude - 1.0;
                    });
                    break;
                case BuffType::UnitActionRateMult:  // 加算（射速）
                    forParam(b.param, [&](int t) {
                        m.actionRateMult[static_cast<size_t>(t)] += b.magnitude - 1.0;
                    });
                    break;
                case BuffType::ProjectileCountExtra:  // 条数
                    forParam(b.param, [&](int t) {
                        m.projectileCountExtra[static_cast<size_t>(t)] +=
                            static_cast<int>(b.magnitude);
                    });
                    break;
                // ---- 标量 ----
                case BuffType::SeaChanceMult: m.seaChanceMult *= b.magnitude; break;
                case BuffType::BounceChanceMult: m.bounceChanceMult *= b.magnitude; break;
                case BuffType::FreeArmyChanceMult: m.freeArmyChanceMult *= b.magnitude; break;
                case BuffType::EconomyGainMult: m.economyGainMult += b.magnitude - 1.0; break;
                case BuffType::BombRadiusMult: m.bombRadiusMult += b.magnitude - 1.0; break;
                case BuffType::LaserDurationMult: m.laserDurationMult += b.magnitude - 1.0; break;
                case BuffType::LaserLengthMult: m.laserLengthMult += b.magnitude - 1.0; break;
                case BuffType::LaserWidthMult: m.laserWidthMult += b.magnitude - 1.0; break;
                case BuffType::MineTimeoutMult: m.mineTimeoutMult += b.magnitude - 1.0; break;
                case BuffType::TechGainMult: m.techGainMult += b.magnitude - 1.0; break;
                case BuffType::LaserExtraBeams:
                    m.laserExtraBeams += static_cast<int>(b.magnitude);
                    break;
            }
        }
    }
    return m;
}

std::vector<Buff> techBuffs(const Config& cfg, const std::vector<int>& levels) {
    std::vector<Buff> out;
    const size_t n = std::min(levels.size(), cfg.tech.techs.size());
    for (size_t i = 0; i < n; ++i) {
        if (levels[i] <= 0) continue;
        const auto& def = cfg.tech.techs[i];
        const size_t level = static_cast<size_t>(levels[i]);
        if (level > def.levels.size()) continue;  // 防御：等级越界（config 变更）
        const auto& lv = def.levels[level - 1];
        out.push_back(Buff{lv.type, lv.param, lv.magnitude, 1, "tech:" + def.id});
    }
    return out;
}

// 数值文案（科研弹窗/科技面板用）：加算/乘算 "+X%"/"-X%"（按累计幅度 (m-1)*100 取整，
// 如 +30%、-10%），条数 "+N"（如 +2）。
std::string buffValueText(BuffType type, double magnitude) {
    char buf[32];
    switch (kindOf(type)) {
        case BuffKind::Count: {
            std::snprintf(buf, sizeof(buf), "+%d", static_cast<int>(magnitude));
            break;
        }
        case BuffKind::Multiplicative:
        case BuffKind::Additive:
        default: {
            const int pct = static_cast<int>(std::lround((magnitude - 1.0) * 100.0));
            std::snprintf(buf, sizeof(buf), "%s%d%%", pct >= 0 ? "+" : "", pct);
            break;
        }
    }
    return std::string(buf);
}

std::vector<Buff> initialFactionBuffs(const Config::Faction& def) {
    std::vector<Buff> out;
    // 全兵修饰符在前、特定兵种在后（聚合时全局先乘、兵种后乘，与迁移前速度链顺序一致）。
    if (def.speedMultAll != 1.0)
        out.push_back(
            {BuffType::UnitSpeedMult, -1, def.speedMultAll, 1, "faction"});
    if (def.pioneerSpeedMult != 1.0)
        out.push_back({BuffType::UnitSpeedMult, static_cast<int>(ArmyType::pioneer),
                       def.pioneerSpeedMult, 1, "faction"});
    if (def.seaMult != 1.0)
        out.push_back({BuffType::SeaChanceMult, -1, def.seaMult, 1, "faction"});
    if (def.bounceMultAll != 1.0)
        out.push_back({BuffType::BounceChanceMult, -1, def.bounceMultAll, 1, "faction"});
    if (def.extraLaserBeams != 0)
        out.push_back({BuffType::LaserExtraBeams, -1,
                       static_cast<double>(def.extraLaserBeams), 1, "faction"});
    if (def.laserDurationMult != 1.0)
        out.push_back({BuffType::LaserDurationMult, -1, def.laserDurationMult, 1, "faction"});
    if (def.laserLengthMult != 1.0)
        out.push_back({BuffType::LaserLengthMult, -1, def.laserLengthMult, 1, "faction"});
    return out;
}

}  // namespace lw
