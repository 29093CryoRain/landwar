#include "core/Config.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace lw {

namespace {

using Json = nlohmann::json;

// 数值读取辅助：缺键/类型不符时返回默认值。
double getNum(const Json& j, const char* key, double def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
    return def;
}
int getInt(const Json& j, const char* key, int def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<int>();
    return def;
}
std::string getStr(const Json& j, const char* key, const std::string& def) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}
bool getBool(const Json& j, const char* key, bool def) {
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    return def;
}

int unitIndex(const std::string& name) {
    if (name == "normal") return static_cast<int>(ArmyType::normal);
    if (name == "vanguard") return static_cast<int>(ArmyType::vanguard);
    if (name == "pioneer") return static_cast<int>(ArmyType::pioneer);
    if (name == "laser") return static_cast<int>(ArmyType::laser);
    if (name == "bomb") return static_cast<int>(ArmyType::bomb);
    if (name == "mine") return static_cast<int>(ArmyType::mine);
    if (name == "pistol") return static_cast<int>(ArmyType::pistol);
    if (name == "shotgun") return static_cast<int>(ArmyType::shotgun);
    return -1;
}

std::string unitName(int idx) {
    switch (idx) {
        case static_cast<int>(ArmyType::normal): return "normal";
        case static_cast<int>(ArmyType::vanguard): return "vanguard";
        case static_cast<int>(ArmyType::pioneer): return "pioneer";
        case static_cast<int>(ArmyType::laser): return "laser";
        case static_cast<int>(ArmyType::bomb): return "bomb";
        case static_cast<int>(ArmyType::mine): return "mine";
        case static_cast<int>(ArmyType::pistol): return "pistol";
        case static_cast<int>(ArmyType::shotgun): return "shotgun";
        default: return "?";
    }
}

// P9 行为枚举字符串映射（config JSON ↔ 枚举）。未知值回退默认（none）。
std::string deathEffectName(DeathEffect de) {
    switch (de) {
        case DeathEffect::laser: return "laser";
        case DeathEffect::bomb: return "bomb";
        case DeathEffect::mine: return "mine";
        case DeathEffect::none: return "none";
    }
    return "none";
}
DeathEffect deathEffectFromName(const std::string& s) {
    if (s == "laser") return DeathEffect::laser;
    if (s == "bomb") return DeathEffect::bomb;
    if (s == "mine") return DeathEffect::mine;
    return DeathEffect::none;
}
std::string periodicName(PeriodicAction p) {
    switch (p) {
        case PeriodicAction::firePistol: return "pistol";
        case PeriodicAction::fireShotgun: return "shotgun";
        case PeriodicAction::none: return "none";
    }
    return "none";
}
PeriodicAction periodicFromName(const std::string& s) {
    if (s == "pistol") return PeriodicAction::firePistol;
    if (s == "shotgun") return PeriodicAction::fireShotgun;
    return PeriodicAction::none;
}

// P8 科技：BuffType 枚举字符串映射（config JSON ↔ 枚举）。未知值回退 UnitSpeedMult（默认）。
std::string buffTypeName(BuffType t) {
    switch (t) {
        case BuffType::UnitCostMult: return "UnitCostMult";
        case BuffType::UnitSpeedMult: return "UnitSpeedMult";
        case BuffType::SeaChanceMult: return "SeaChanceMult";
        case BuffType::BounceChanceMult: return "BounceChanceMult";
        case BuffType::EconomyGainMult: return "EconomyGainMult";
        case BuffType::BombRadiusMult: return "BombRadiusMult";
        case BuffType::LaserDurationMult: return "LaserDurationMult";
        case BuffType::LaserLengthMult: return "LaserLengthMult";
        case BuffType::LaserWidthMult: return "LaserWidthMult";
        case BuffType::LaserExtraBeams: return "LaserExtraBeams";
        case BuffType::MineTimeoutMult: return "MineTimeoutMult";
        case BuffType::FreeArmyChanceMult: return "FreeArmyChanceMult";
        case BuffType::UnitActionRateMult: return "UnitActionRateMult";
        case BuffType::ProjectileCountExtra: return "ProjectileCountExtra";
        case BuffType::TechGainMult: return "TechGainMult";
    }
    return "UnitSpeedMult";
}
BuffType buffTypeFromName(const std::string& s) {
    if (s == "UnitCostMult") return BuffType::UnitCostMult;
    if (s == "UnitSpeedMult") return BuffType::UnitSpeedMult;
    if (s == "SeaChanceMult") return BuffType::SeaChanceMult;
    if (s == "BounceChanceMult") return BuffType::BounceChanceMult;
    if (s == "EconomyGainMult") return BuffType::EconomyGainMult;
    if (s == "BombRadiusMult") return BuffType::BombRadiusMult;
    if (s == "LaserDurationMult") return BuffType::LaserDurationMult;
    if (s == "LaserLengthMult") return BuffType::LaserLengthMult;
    if (s == "LaserWidthMult") return BuffType::LaserWidthMult;
    if (s == "LaserExtraBeams") return BuffType::LaserExtraBeams;
    if (s == "MineTimeoutMult") return BuffType::MineTimeoutMult;
    if (s == "FreeArmyChanceMult") return BuffType::FreeArmyChanceMult;
    if (s == "UnitActionRateMult") return BuffType::UnitActionRateMult;
    if (s == "ProjectileCountExtra") return BuffType::ProjectileCountExtra;
    if (s == "TechGainMult") return BuffType::TechGainMult;
    return BuffType::UnitSpeedMult;
}

// 内置默认势力表（0..8，下标即 id），与翻新计划 §2.7/§2.8 一致。
void initDefaultFactions(std::vector<Config::Faction>& v) {
    v.clear();
    v.resize(static_cast<std::size_t>(kFactionTotal));

    auto& neutral = v[0];
    neutral.id = 0;
    neutral.color = {127, 127, 127};

    auto& red = v[1];
    red.id = 1;
    red.color = {255, 0, 0};
    red.unitPreference[0] = 2.0;  // 普通兵偏好 2（用户定夺 2026-08-08）

    auto& yellow = v[2];
    yellow.id = 2;
    yellow.color = {255, 255, 0};
    yellow.unitPreference[3] = 1.5;  // 激光
    yellow.unitPreference[4] = 1.5;  // 炸弹
    yellow.unitPreference[5] = 1.5;  // 地雷

    auto& cyan = v[3];
    cyan.id = 3;
    cyan.color = {0, 255, 255};
    cyan.unitPreference[1] = 3.0;  // 先锋（特色）
    cyan.speedMultAll = 1.5;
    cyan.seaMult = 0.666;

    auto& blue = v[4];
    blue.id = 4;
    blue.color = {0, 85, 255};
    blue.unitPreference[2] = 3.0;  // 开拓（特色）
    blue.unitPreference[1] = 1.5;  // 先锋（用户增注）
    blue.bounceMultAll = 0.6;
    blue.pioneerSpeedMult = 2.0;

    auto& green = v[5];
    green.id = 5;
    green.color = {0, 255, 0};
    green.unitPreference[3] = 3.0;  // 激光（特色）
    green.extraLaserBeams = 2;
    green.laserDurationMult = 1.5;
    green.laserLengthMult = 1.5;

    auto& orange = v[6];
    orange.id = 6;
    orange.color = {255, 127, 0};
    orange.unitPreference[4] = 3.0;  // 炸弹（特色）
    orange.bombRadius = 4.08;

    auto& purple = v[7];
    purple.id = 7;
    purple.color = {127, 0, 255};
    purple.unitPreference[5] = 3.0;  // 地雷（特色）
    purple.mineTriggerRadius = 1.43;

    auto& magenta = v[8];
    magenta.id = 8;
    magenta.color = {255, 0, 255};
    magenta.freeArmyChance = 0.6;
}

}  // namespace

Config Config::loadFromJson(const std::string& jsonText) {
    Config cfg;
    initDefaultFactions(cfg.factions);

    Json root;
    try {
        root = Json::parse(jsonText);
    } catch (const std::exception& e) {
        spdlog::warn("config.json parse failed ({}), using defaults", e.what());
        return cfg;
    }

    // ---- map ----
    if (root.contains("map") && root["map"].is_object()) {
        const auto& mapJson = root["map"];
        cfg.map.file = getStr(mapJson, "file", cfg.map.file);
        cfg.map.width = getInt(mapJson, "width", cfg.map.width);
        cfg.map.height = getInt(mapJson, "height", cfg.map.height);
        cfg.map.blockSize = getInt(mapJson, "blockSize", cfg.map.blockSize);
        cfg.map.panelWidth = getInt(mapJson, "panelWidth", cfg.map.panelWidth);
        cfg.map.capitalMinDistance =
            getInt(mapJson, "capitalMinDistance", cfg.map.capitalMinDistance);
    }

    // ---- army ----
    if (root.contains("army") && root["army"].is_object()) {
        const auto& armyJson = root["army"];
        cfg.army.baseSpeed = getNum(armyJson, "baseSpeed", cfg.army.baseSpeed);
        cfg.army.baseSize = getNum(armyJson, "baseSize", cfg.army.baseSize);
        cfg.army.bounceJitterHalfRange = getNum(armyJson, "bounceJitterHalfRange",
                                                cfg.army.bounceJitterHalfRange);
        cfg.army.bounceJitterDenominator = getNum(armyJson, "bounceJitterDenominator",
                                                  cfg.army.bounceJitterDenominator);
    }

    // ---- sea ----
    if (root.contains("sea") && root["sea"].is_object()) {
        const auto& seaJson = root["sea"];
        cfg.sea.initialGoSeaProbability = getNum(seaJson, "initialGoSeaProbability",
                                                 cfg.sea.initialGoSeaProbability);
        cfg.sea.goSeaIncrease = getNum(seaJson, "goSeaIncrease", cfg.sea.goSeaIncrease);
        cfg.sea.goSeaChanceConst = getNum(seaJson, "goSeaChanceConst", cfg.sea.goSeaChanceConst);
        cfg.sea.goSeaChanceDenominator =
            getNum(seaJson, "goSeaChanceDenominator", cfg.sea.goSeaChanceDenominator);
        cfg.sea.cyanSeaMult = getNum(seaJson, "cyanSeaMult", cfg.sea.cyanSeaMult);
        cfg.sea.seaSpeedMult = getNum(seaJson, "seaSpeedMult", cfg.sea.seaSpeedMult);
    }

    // ---- terrain（地形基图 P5 改版）----
    if (root.contains("terrain") && root["terrain"].is_object()) {
        const auto& tJson = root["terrain"];
        cfg.terrain.seaChannelMin = getInt(tJson, "seaChannelMin", cfg.terrain.seaChannelMin);
        cfg.terrain.probFloor = getInt(tJson, "probFloor", cfg.terrain.probFloor);
        cfg.terrain.probScale = getInt(tJson, "probScale", cfg.terrain.probScale);
        cfg.terrain.mountainEnterChance =
            getNum(tJson, "mountainEnterChance", cfg.terrain.mountainEnterChance);
        cfg.terrain.mountainSpeedMult =
            getNum(tJson, "mountainSpeedMult", cfg.terrain.mountainSpeedMult);
    }

    // ---- units ----
    if (root.contains("units") && root["units"].is_array()) {
        for (const auto& unitJson : root["units"]) {
            if (!unitJson.is_object()) continue;
            const int idx = unitIndex(getStr(unitJson, "type", ""));
            if (idx < 0 || idx >= static_cast<int>(cfg.units.size())) continue;
            cfg.units[idx].cost = getNum(unitJson, "cost", cfg.units[idx].cost);
            cfg.units[idx].speedMult = getNum(unitJson, "speedMult", cfg.units[idx].speedMult);
            cfg.units[idx].sizeMult = getNum(unitJson, "sizeMult", cfg.units[idx].sizeMult);
            cfg.units[idx].bounceMult = getNum(unitJson, "bounceMult", cfg.units[idx].bounceMult);
            cfg.units[idx].visualRadius =
                getNum(unitJson, "visualRadius", cfg.units[idx].visualRadius);
            cfg.units[idx].mountainEnterMult =
                getNum(unitJson, "mountainEnterMult", cfg.units[idx].mountainEnterMult);
            cfg.units[idx].mountainNoSlow =
                getBool(unitJson, "mountainNoSlow", cfg.units[idx].mountainNoSlow);
            // P9 行为：死亡特效 / 周期动作 / 间隔（缺键保持默认）。
            cfg.units[idx].deathEffect =
                deathEffectFromName(getStr(unitJson, "deathEffect",
                                           deathEffectName(cfg.units[idx].deathEffect)));
            cfg.units[idx].periodic =
                periodicFromName(getStr(unitJson, "periodic",
                                        periodicName(cfg.units[idx].periodic)));
            cfg.units[idx].periodTicks = getInt(unitJson, "periodTicks", cfg.units[idx].periodTicks);
            // P9 射弹参数（发射兵用；缺键保持默认）。
            cfg.units[idx].bulletSpeed =
                getNum(unitJson, "bulletSpeed", cfg.units[idx].bulletSpeed);
            cfg.units[idx].bulletSpeedJitter =
                getNum(unitJson, "bulletSpeedJitter", cfg.units[idx].bulletSpeedJitter);
            cfg.units[idx].bulletCount =
                getInt(unitJson, "bulletCount", cfg.units[idx].bulletCount);
            cfg.units[idx].bulletLifespanTicks =
                getInt(unitJson, "bulletLifespanTicks", cfg.units[idx].bulletLifespanTicks);
            cfg.units[idx].bulletSize =
                getNum(unitJson, "bulletSize", cfg.units[idx].bulletSize);
            cfg.units[idx].bulletSpreadPIFrac =
                getNum(unitJson, "bulletSpreadPIFrac", cfg.units[idx].bulletSpreadPIFrac);
            cfg.units[idx].bulletSpreadJitterFrac =
                getNum(unitJson, "bulletSpreadJitterFrac", cfg.units[idx].bulletSpreadJitterFrac);
        }
    }

    // ---- factions ----
    if (root.contains("factions") && root["factions"].is_array()) {
        for (const auto& factionJson : root["factions"]) {
            if (!factionJson.is_object()) continue;
            const int id = getInt(factionJson, "id", -1);
            if (id < 0 || id >= kFactionTotal) continue;
            auto& fdef = cfg.factions[static_cast<std::size_t>(id)];
            if (factionJson.contains("color") && factionJson["color"].is_array()
                && factionJson["color"].size() == 3) {
                fdef.color[0] = factionJson["color"][0].get<int>();
                fdef.color[1] = factionJson["color"][1].get<int>();
                fdef.color[2] = factionJson["color"][2].get<int>();
            }
            // P11 改版：兵种偏好系数（原 costDiv 价格折扣已移除）。
            if (factionJson.contains("unitPreference") && factionJson["unitPreference"].is_object()) {
                for (const auto& [name, value] : factionJson["unitPreference"].items()) {
                    const int ui = unitIndex(name);
                    if (ui >= 0 && value.is_number()) {
                        fdef.unitPreference[ui] = value.get<double>();
                    }
                }
            }
            fdef.speedMultAll = getNum(factionJson, "speedMultAll", fdef.speedMultAll);
            fdef.seaMult = getNum(factionJson, "seaMult", fdef.seaMult);
            fdef.bounceMultAll = getNum(factionJson, "bounceMultAll", fdef.bounceMultAll);
            fdef.pioneerSpeedMult =
                getNum(factionJson, "pioneerSpeedMult", fdef.pioneerSpeedMult);
            fdef.extraLaserBeams = getInt(factionJson, "extraLaserBeams", fdef.extraLaserBeams);
            fdef.laserDurationMult =
                getNum(factionJson, "laserDurationMult", fdef.laserDurationMult);
            fdef.laserLengthMult = getNum(factionJson, "laserLengthMult", fdef.laserLengthMult);
            fdef.bombRadius = getNum(factionJson, "bombRadius", fdef.bombRadius);
            fdef.mineTriggerRadius =
                getNum(factionJson, "mineTriggerRadius", fdef.mineTriggerRadius);
            fdef.freeArmyChance = getNum(factionJson, "freeArmyChance", fdef.freeArmyChance);
        }
    }

    // ---- effect ----
    if (root.contains("effect") && root["effect"].is_object()) {
        const auto& effectJson = root["effect"];
        if (effectJson.contains("bomb") && effectJson["bomb"].is_object()) {
            cfg.effect.bomb.lifetimeTicks =
                getInt(effectJson["bomb"], "lifetimeTicks", cfg.effect.bomb.lifetimeTicks);
            cfg.effect.bomb.conquerEveryTicks = getInt(effectJson["bomb"], "conquerEveryTicks",
                                                       cfg.effect.bomb.conquerEveryTicks);
            cfg.effect.bomb.baseRadius =
                getNum(effectJson["bomb"], "baseRadius", cfg.effect.bomb.baseRadius);
        }
        if (effectJson.contains("mine") && effectJson["mine"].is_object()) {
            const auto& mineJson = effectJson["mine"];
            cfg.effect.mine.radius = getNum(mineJson, "radius", cfg.effect.mine.radius);
            cfg.effect.mine.armTicks = getInt(mineJson, "armTicks", cfg.effect.mine.armTicks);
            cfg.effect.mine.checkEveryTicks =
                getInt(mineJson, "checkEveryTicks", cfg.effect.mine.checkEveryTicks);
            cfg.effect.mine.timeoutTicks =
                getInt(mineJson, "timeoutTicks", cfg.effect.mine.timeoutTicks);
            cfg.effect.mine.triggerRadiusMult =
                getNum(mineJson, "triggerRadiusMult", cfg.effect.mine.triggerRadiusMult);
            cfg.effect.mine.triggerBombRadius =
                getNum(mineJson, "triggerBombRadius", cfg.effect.mine.triggerBombRadius);
        }
        if (effectJson.contains("laser") && effectJson["laser"].is_object()) {
            const auto& laserJson = effectJson["laser"];
            cfg.effect.laser.durationTicks =
                getInt(laserJson, "durationTicks", cfg.effect.laser.durationTicks);
            cfg.effect.laser.length = getNum(laserJson, "length", cfg.effect.laser.length);
            cfg.effect.laser.extendPerTick =
                getInt(laserJson, "extendPerTick", cfg.effect.laser.extendPerTick);
            cfg.effect.laser.killExtraRadius =
                getNum(laserJson, "killExtraRadius", cfg.effect.laser.killExtraRadius);
            cfg.effect.laser.beamSpreadPIFrac =
                getNum(laserJson, "beamSpreadPIFrac", cfg.effect.laser.beamSpreadPIFrac);
        }
    }

    // ---- projectile（P9 射弹全局参数）----
    if (root.contains("projectile") && root["projectile"].is_object()) {
        const auto& pj = root["projectile"];
        cfg.projectile.drawSize = getInt(pj, "drawSize", cfg.projectile.drawSize);
        cfg.projectile.mountainLifespanPenalty =
            getInt(pj, "mountainLifespanPenalty", cfg.projectile.mountainLifespanPenalty);
    }

    // ---- economy（P14 经济重构：朴素逐 tick 收入；删旧魔法公式键）----
    if (root.contains("economy") && root["economy"].is_object()) {
        const auto& economyJson = root["economy"];
        cfg.economy.initialEconomy =
            getNum(economyJson, "initialEconomy", cfg.economy.initialEconomy);
        cfg.economy.perLandIncome =
            getNum(economyJson, "perLandIncome", cfg.economy.perLandIncome);
        cfg.economy.cityBaseMult =
            getNum(economyJson, "cityBaseMult", cfg.economy.cityBaseMult);
        cfg.economy.capitalBase = getNum(economyJson, "capitalBase", cfg.economy.capitalBase);
    }

    // ---- city（P13 城市系统：等级幂律指数 + 形状表）----
    if (root.contains("city") && root["city"].is_object()) {
        const auto& cityJson = root["city"];
        cfg.city.levelIncomeExponent =
            getNum(cityJson, "levelIncomeExponent", cfg.city.levelIncomeExponent);
        cfg.city.levelRankExponent =
            getNum(cityJson, "levelRankExponent", cfg.city.levelRankExponent);
        // shapes 覆盖（可选）：[{level, w, h}, ...]；缺省保持内置表。按 level 匹配定位。
        if (cityJson.contains("shapes") && cityJson["shapes"].is_array()) {
            for (const auto& s : cityJson["shapes"]) {
                if (!s.is_object()) continue;
                const int lv = getInt(s, "level", -1);
                const int idx = cfg.city.levelIndex(lv);
                if (idx < 0) continue;
                cfg.city.shapes[static_cast<size_t>(idx)] = {
                    getInt(s, "w", cfg.city.shapes[static_cast<size_t>(idx)][0]),
                    getInt(s, "h", cfg.city.shapes[static_cast<size_t>(idx)][1])};
            }
        }
    }

    // ---- capital（P15 迁都状态机）----
    if (root.contains("capital") && root["capital"].is_object()) {
        const auto& capJ = root["capital"];
        cfg.capital.relocationDelayTicks =
            getNum(capJ, "relocationDelayTicks", cfg.capital.relocationDelayTicks);
    }

    // ---- sim ----
    if (root.contains("sim") && root["sim"].is_object()) {
        const auto& simJson = root["sim"];
        cfg.sim.tickRate = getNum(simJson, "tickRate", cfg.sim.tickRate);
        cfg.sim.maxArmyGuard = getInt(simJson, "maxArmyGuard", cfg.sim.maxArmyGuard);
    }

    // ---- render ----
    if (root.contains("render") && root["render"].is_object()) {
        const auto& renderJson = root["render"];
        cfg.render.windowWidth = getInt(renderJson, "windowWidth", cfg.render.windowWidth);
        cfg.render.windowHeight = getInt(renderJson, "windowHeight", cfg.render.windowHeight);
        cfg.render.spriteSize = getInt(renderJson, "spriteSize", cfg.render.spriteSize);
        cfg.render.armyDrawSize = getInt(renderJson, "armyDrawSize", cfg.render.armyDrawSize);
        // P13 城市渲染常数（lineMinCellPx/minIconSizePx/lineThickness/iconScale[]）。
        if (renderJson.contains("city") && renderJson["city"].is_object()) {
            const auto& rc = renderJson["city"];
            cfg.render.city.lineMinCellPx =
                getNum(rc, "lineMinCellPx", cfg.render.city.lineMinCellPx);
            cfg.render.city.lineThickness =
                getNum(rc, "lineThickness", cfg.render.city.lineThickness);
            cfg.render.city.lineDarken = getNum(rc, "lineDarken", cfg.render.city.lineDarken);
            cfg.render.city.iconDarken = getNum(rc, "iconDarken", cfg.render.city.iconDarken);
            // iconScale[]：9 项（下标 = 等级-1，与 data/tower/tower<N>.png 对应）。
            if (rc.contains("iconScale") && rc["iconScale"].is_array())
                for (std::size_t i = 0; i < cfg.render.city.iconScale.size() && i < rc["iconScale"].size();
                     ++i)
                    if (rc["iconScale"][i].is_number())
                        cfg.render.city.iconScale[i] = rc["iconScale"][i].get<double>();
        }
        // P15 首都渲染常数（render.capital）。
        if (renderJson.contains("capital") && renderJson["capital"].is_object()) {
            const auto& rcap = renderJson["capital"];
            cfg.render.capital.minIconSizePx =
                getInt(rcap, "minIconSizePx", cfg.render.capital.minIconSizePx);
            cfg.render.capital.lineThickness =
                getNum(rcap, "lineThickness", cfg.render.capital.lineThickness);
            cfg.render.capital.designatedAlpha =
                getNum(rcap, "designatedAlpha", cfg.render.capital.designatedAlpha);
            if (rcap.contains("iconScale") && rcap["iconScale"].is_array())
                for (std::size_t i = 0;
                     i < cfg.render.capital.iconScale.size() && i < rcap["iconScale"].size(); ++i)
                    if (rcap["iconScale"][i].is_number())
                        cfg.render.capital.iconScale[i] = rcap["iconScale"][i].get<double>();
        }
    }

    // ---- player ----
    if (root.contains("player") && root["player"].is_object()) {
        const auto& playerJson = root["player"];
        cfg.player.hoverCityRadius =
            getNum(playerJson, "hoverCityRadius", cfg.player.hoverCityRadius);
        cfg.player.markerAlpha = getNum(playerJson, "markerAlpha", cfg.player.markerAlpha);
        cfg.player.markerRotateSpeed =
            getNum(playerJson, "markerRotateSpeed", cfg.player.markerRotateSpeed);
        cfg.player.markerRingMargin =
            getNum(playerJson, "markerRingMargin", cfg.player.markerRingMargin);
        cfg.player.markerArrowGap =
            getNum(playerJson, "markerArrowGap", cfg.player.markerArrowGap);
    }

    // ---- ui（消息面板 P4）----
    if (root.contains("ui") && root["ui"].is_object()) {
        const auto& uiJson = root["ui"];
        cfg.ui.messageMaxShown = getInt(uiJson, "messageMaxShown", cfg.ui.messageMaxShown);
    }

    // ---- tech（P8 科技系统：阈值门槛 + 科技表，数据驱动）----
    if (root.contains("tech") && root["tech"].is_object()) {
        const auto& techJson = root["tech"];
        cfg.tech.thresholdBase = getNum(techJson, "thresholdBase", cfg.tech.thresholdBase);
        cfg.tech.thresholdStep = getNum(techJson, "thresholdStep", cfg.tech.thresholdStep);
        cfg.tech.pointsPerCityLevel =
            getNum(techJson, "pointsPerCityLevel", cfg.tech.pointsPerCityLevel);
        cfg.tech.preferencePerLevel =
            getNum(techJson, "preferencePerLevel", cfg.tech.preferencePerLevel);
        cfg.tech.playerCandidateCount =
            getInt(techJson, "playerCandidateCount", cfg.tech.playerCandidateCount);
        if (techJson.contains("techs") && techJson["techs"].is_array()) {
            for (const auto& tj : techJson["techs"]) {
                if (!tj.is_object()) continue;
                Config::Tech::TechDef def;
                def.id = getStr(tj, "id", "");
                def.name = getStr(tj, "name", def.id);
                def.desc = getStr(tj, "desc", "");
                if (tj.contains("levels") && tj["levels"].is_array()) {
                    for (const auto& lj : tj["levels"]) {
                        if (!lj.is_object()) continue;
                        Config::Tech::Level lv;
                        lv.type = buffTypeFromName(getStr(lj, "type", ""));
                        const std::string param = getStr(lj, "param", "all");
                        lv.param = (param == "all") ? -1 : unitIndex(param);
                        lv.magnitude = getNum(lj, "magnitude", lv.magnitude);
                        def.levels.push_back(lv);
                    }
                }
                if (tj.contains("preferenceUnits") && tj["preferenceUnits"].is_array()) {
                    for (const auto& u : tj["preferenceUnits"]) {
                        if (!u.is_string()) continue;
                        const int ui = unitIndex(u.get<std::string>());
                        if (ui >= 0) def.preferenceUnits.push_back(ui);
                    }
                }
                cfg.tech.techs.push_back(std::move(def));
            }
        }
    }

    return cfg;
}

Config Config::loadFromFile(const std::string& path) {
    std::ifstream ifs(path);
    // 注意：MinGW 下文件不存在时 operator bool 仍为 true，须用 is_open() 判断。
    if (!ifs.is_open()) {
        spdlog::warn("config file '{}' not found, using built-in defaults", path);
        Config cfg;
        initDefaultFactions(cfg.factions);
        return cfg;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return loadFromJson(oss.str());
}

std::string Config::toJson() const {
    Json j;
    j["map"] = {{"file", map.file},
                {"width", map.width},
                {"height", map.height},
                {"blockSize", map.blockSize},
                {"panelWidth", map.panelWidth},
                {"capitalMinDistance", map.capitalMinDistance}};

    j["army"] = {{"baseSpeed", army.baseSpeed},
                 {"baseSize", army.baseSize},
                 {"bounceJitterHalfRange", army.bounceJitterHalfRange},
                 {"bounceJitterDenominator", army.bounceJitterDenominator}};

    j["sea"] = {{"initialGoSeaProbability", sea.initialGoSeaProbability},
                {"goSeaIncrease", sea.goSeaIncrease},
                {"goSeaChanceConst", sea.goSeaChanceConst},
                {"goSeaChanceDenominator", sea.goSeaChanceDenominator},
                {"cyanSeaMult", sea.cyanSeaMult},
                {"seaSpeedMult", sea.seaSpeedMult}};

    j["terrain"] = {{"seaChannelMin", terrain.seaChannelMin},
                    {"probFloor", terrain.probFloor},
                    {"probScale", terrain.probScale},
                    {"mountainEnterChance", terrain.mountainEnterChance},
                    {"mountainSpeedMult", terrain.mountainSpeedMult}};

    j["units"] = Json::array();
    for (int i = 0; i < kArmyTypeCount; ++i) {
        j["units"].push_back({{"type", unitName(i)},
                              {"cost", units[static_cast<size_t>(i)].cost},
                              {"speedMult", units[static_cast<size_t>(i)].speedMult},
                              {"sizeMult", units[static_cast<size_t>(i)].sizeMult},
                              {"bounceMult", units[static_cast<size_t>(i)].bounceMult},
                              {"visualRadius", units[static_cast<size_t>(i)].visualRadius},
                              {"mountainEnterMult", units[static_cast<size_t>(i)].mountainEnterMult},
                              {"mountainNoSlow", units[static_cast<size_t>(i)].mountainNoSlow},
                              {"deathEffect", deathEffectName(units[static_cast<size_t>(i)].deathEffect)},
                              {"periodic", periodicName(units[static_cast<size_t>(i)].periodic)},
                              {"periodTicks", units[static_cast<size_t>(i)].periodTicks},
                              {"bulletSpeed", units[static_cast<size_t>(i)].bulletSpeed},
                              {"bulletSpeedJitter", units[static_cast<size_t>(i)].bulletSpeedJitter},
                              {"bulletCount", units[static_cast<size_t>(i)].bulletCount},
                              {"bulletLifespanTicks", units[static_cast<size_t>(i)].bulletLifespanTicks},
                              {"bulletSize", units[static_cast<size_t>(i)].bulletSize},
                              {"bulletSpreadPIFrac", units[static_cast<size_t>(i)].bulletSpreadPIFrac},
                              {"bulletSpreadJitterFrac", units[static_cast<size_t>(i)].bulletSpreadJitterFrac}});
    }

    j["factions"] = Json::array();
    for (const auto& f : factions) {
        Json fj;
        fj["id"] = f.id;
        fj["color"] = {f.color[0], f.color[1], f.color[2]};
        Json up;
        for (int i = 0; i < kArmyTypeCount; ++i)
            up[unitName(i)] = f.unitPreference[static_cast<size_t>(i)];
        fj["unitPreference"] = std::move(up);
        fj["speedMultAll"] = f.speedMultAll;
        fj["seaMult"] = f.seaMult;
        fj["bounceMultAll"] = f.bounceMultAll;
        fj["pioneerSpeedMult"] = f.pioneerSpeedMult;
        fj["extraLaserBeams"] = f.extraLaserBeams;
        fj["laserDurationMult"] = f.laserDurationMult;
        fj["laserLengthMult"] = f.laserLengthMult;
        fj["bombRadius"] = f.bombRadius;
        fj["mineTriggerRadius"] = f.mineTriggerRadius;
        fj["freeArmyChance"] = f.freeArmyChance;
        j["factions"].push_back(std::move(fj));
    }

    j["effect"]["bomb"] = {{"lifetimeTicks", effect.bomb.lifetimeTicks},
                           {"conquerEveryTicks", effect.bomb.conquerEveryTicks},
                           {"baseRadius", effect.bomb.baseRadius}};
    j["effect"]["mine"] = {{"radius", effect.mine.radius},
                           {"armTicks", effect.mine.armTicks},
                           {"checkEveryTicks", effect.mine.checkEveryTicks},
                           {"timeoutTicks", effect.mine.timeoutTicks},
                           {"triggerRadiusMult", effect.mine.triggerRadiusMult},
                           {"triggerBombRadius", effect.mine.triggerBombRadius}};
    j["effect"]["laser"] = {{"durationTicks", effect.laser.durationTicks},
                            {"length", effect.laser.length},
                            {"extendPerTick", effect.laser.extendPerTick},
                            {"killExtraRadius", effect.laser.killExtraRadius},
                            {"beamSpreadPIFrac", effect.laser.beamSpreadPIFrac}};

    j["projectile"] = {{"drawSize", projectile.drawSize},
                       {"mountainLifespanPenalty", projectile.mountainLifespanPenalty}};

    j["economy"] = {{"initialEconomy", economy.initialEconomy},
                    {"perLandIncome", economy.perLandIncome},
                    {"cityBaseMult", economy.cityBaseMult},
                    {"capitalBase", economy.capitalBase}};

    Json shapesJ = Json::array();
    for (int i = 0; i < static_cast<int>(city.levels.size()); ++i) {
        shapesJ.push_back({{"level", city.levels[static_cast<size_t>(i)]},
                           {"w", city.shapes[static_cast<size_t>(i)][0]},
                           {"h", city.shapes[static_cast<size_t>(i)][1]}});
    }
    j["city"] = {{"levelIncomeExponent", city.levelIncomeExponent},
                 {"levelRankExponent", city.levelRankExponent},
                 {"shapes", std::move(shapesJ)}};

    j["capital"] = {{"relocationDelayTicks", capital.relocationDelayTicks}};

    j["sim"] = {{"tickRate", sim.tickRate}, {"maxArmyGuard", sim.maxArmyGuard}};

    // P13 城市渲染常数：iconScale[]（9 项，下标 = 等级-1，与 data/tower/tower<N>.png 对应）。
    j["render"] = {
        {"windowWidth", render.windowWidth},
        {"windowHeight", render.windowHeight},
        {"spriteSize", render.spriteSize},
        {"armyDrawSize", render.armyDrawSize},
        {"city",
         {{"lineMinCellPx", render.city.lineMinCellPx},
          {"lineThickness", render.city.lineThickness},
          {"lineDarken", render.city.lineDarken},
          {"iconDarken", render.city.iconDarken},
          {"iconScale", std::vector<double>(render.city.iconScale.begin(), render.city.iconScale.end())}}},
        {"capital",
         {{"minIconSizePx", render.capital.minIconSizePx},
          {"lineThickness", render.capital.lineThickness},
          {"designatedAlpha", render.capital.designatedAlpha},
          {"iconScale", std::vector<double>(render.capital.iconScale.begin(),
                                            render.capital.iconScale.end())}}},
    };

    j["player"] = {{"hoverCityRadius", player.hoverCityRadius},
                   {"markerAlpha", player.markerAlpha},
                   {"markerRotateSpeed", player.markerRotateSpeed},
                   {"markerRingMargin", player.markerRingMargin},
                   {"markerArrowGap", player.markerArrowGap}};

    j["ui"] = {{"messageMaxShown", ui.messageMaxShown}};

    // P8 科技：阈值门槛 + 科技表（与 loadFromJson 键对称）。
    j["tech"] = {{"thresholdBase", tech.thresholdBase},
                 {"thresholdStep", tech.thresholdStep},
                 {"pointsPerCityLevel", tech.pointsPerCityLevel},
                 {"preferencePerLevel", tech.preferencePerLevel},
                 {"playerCandidateCount", tech.playerCandidateCount}};
    Json techsJ = Json::array();
    for (const auto& t : tech.techs) {
        Json tj;
        tj["id"] = t.id;
        tj["name"] = t.name;
        tj["desc"] = t.desc;
        Json levelsJ = Json::array();
        for (const auto& lv : t.levels) {
            levelsJ.push_back({{"type", buffTypeName(lv.type)},
                               {"param", lv.param < 0 ? "all" : unitName(lv.param)},
                               {"magnitude", lv.magnitude}});
        }
        tj["levels"] = std::move(levelsJ);
        Json prefJ = Json::array();
        for (int u : t.preferenceUnits) prefJ.push_back(unitName(u));
        tj["preferenceUnits"] = std::move(prefJ);
        techsJ.push_back(std::move(tj));
    }
    j["tech"]["techs"] = std::move(techsJ);

    return j.dump(2);
}

}  // namespace lw
