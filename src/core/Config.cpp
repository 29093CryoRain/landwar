#include "core/Config.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "world/tiling/Tiling.h"

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

// 内置默认城市表（P13 + P12）：等级/形状按密铺。
// ShapeCell 偏移语义（Config.h 注）：方 = 格 (dx,dy)；六 = 轴向 (dq,dr)；三 = (b,h) 单位 + 朝向。
void initDefaultCity(Config::City& c) {
    using Shape = Config::City::Shape;
    using ShapeCell = Config::City::ShapeCell;
    const auto sq = [](int w, int h) {
        Shape sh;
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx < w; ++dx)
                sh.cells.push_back(ShapeCell{static_cast<double>(dx), static_cast<double>(dy), 0});
        return sh;
    };
    const auto hx = [](const std::vector<std::pair<int, int>>& ax) {
        Shape sh;
        for (const auto& p : ax)
            sh.cells.push_back(ShapeCell{static_cast<double>(p.first),
                                         static_cast<double>(p.second), 0});
        return sh;
    };
    const auto tr = [](const std::vector<std::array<double, 3>>& cells) {
        Shape sh;
        for (const auto& c0 : cells)
            sh.cells.push_back(ShapeCell{c0[0], c0[1], static_cast<int>(c0[2])});
        return sh;
    };
    // 正方形：1/2/4/6/9（旧 w×h：1×1 / 1×2 / 2×2 / 2×3 / 3×3）。
    c.square.levels = {1, 2, 4, 6, 9};
    c.square.shapes = {sq(1, 1), sq(1, 2), sq(2, 2), sq(2, 3), sq(3, 3)};
    // 六边形：1/3/4/6/7/9（轴向偏移表，P12 §3.2 定稿；锚 = (0,0)）。
    c.hex.levels = {1, 3, 4, 6, 7, 9};
    c.hex.shapes = {
        hx({{0, 0}}),
        hx({{0, 0}, {0, -1}, {1, -1}}),
        hx({{0, 0}, {0, -1}, {1, -1}, {1, -2}}),
        hx({{0, 0}, {0, -1}, {1, -1}, {1, -2}, {0, -2}, {2, -2}}),
        hx({{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, -1}, {-1, 1}}),
        hx({{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, -1}, {-1, 1}, {-1, 2}, {1, -2}}),
    };
    // 三角形：1/2/4/6/8（世界偏移以 b/h 计 + 朝向 0 正/1 反；**正锚模式**——表按"锚 =
    // 正三角"定义；锚为反三角时解析端对 dy 垂直镜像、orient 翻转，即该等级的朝向变体）。
    // 语义（用户 2026-08 定稿）：
    //   L1 = 1 格（锚正→正、锚反→反，2 种模式）
    //   L2 = 一正一反，公共边**水平**（正底边 = 反底边，同列上下相邻）
    //   L4 = 边长 2b 大三角含 4 小三角（锚正→尖朝上、锚反→尖朝下，2 种模式）
    //   L6 = 6 格正六边形（左右顶点、上下平边；3 正 3 反；锚镜像同形，1 种模式）
    //   L8 = L6 + 顶上正 + 底下反（≡ 两个 4 级城拼接；锚镜像旋转 180° 同构，1 种模式）
    c.tri.levels = {1, 2, 4, 6, 8};
    // 2026-08-16：已接入几何的 5 种阿基米德密铺先给 1 级城（单格）占位，
    // 完整等级形状表待各密铺城市基建形状数据补全。
    {
        const auto single = [] {
            Shape sh;
            sh.cells.push_back(ShapeCell{0.0, 0.0, 0});
            return sh;
        };
        for (const TilingType t :
             {TilingType::Arch3464, TilingType::Arch3636, TilingType::Arch31212,
              TilingType::Arch4612, TilingType::Arch488, TilingType::Laves3464,
              TilingType::Laves3636, TilingType::Laves31212, TilingType::Laves4612,
              TilingType::Laves488}) {
            auto& set = c.sets[static_cast<size_t>(static_cast<int>(t))];
            set.levels = {1.0};
            set.shapes = {single()};
        }
        // 完整城市形状表构造辅助（2026-08-16）。
        const auto shp = [](int anchorN, const std::vector<std::array<double, 3>>& cells) {
            Shape sh;
            sh.anchorN = anchorN;
            for (const auto& cl : cells)
                sh.cells.push_back(ShapeCell{cl[0], cl[1], static_cast<int>(cl[2])});
            return sh;
        };
        // Arch488（4.8.8）完整城市形状表。anchorN = 锚点格边数：
        // 8 = 八边形锚（L2/L3/L4）、4 = 正方形锚（L7/L8）。偏移为“目标格中心−锚格中心”。
        {
            auto& a488 = c.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch488))];
            a488.levels = {1.6568542494923795, 3.0294372515228583, 4.0, 6.970562748477137,
                           8.343145750507617};
            a488.shapes = {
                shp(8, {{0.0, 0.0, 0}}),
                shp(8, {{-0.707106781187, -0.707106781187, 0},
                        {-0.707106781187, 0.707106718813, 0},
                        {0.0, 0.0, 0},
                        {0.707106718813, -0.707106781187, 0},
                        {0.707106718813, 0.707106781187, 0}}),
                shp(8, {{-0.707106781187, -0.707106781187, 0},
                        {0.0, -1.4142135, 0},
                        {0.0, 0.0, 0},
                        {0.707106718813, -0.707106781187, 0}}),
                shp(4, {{-0.707106718813, -0.707106718813, 0},
                        {-0.707106718813, 0.707106781187, 0},
                        {0.0, 0.0, 0},
                        {0.707106781187, -0.707106718813, 0},
                        {0.707106781187, 0.707106781187, 0}}),
                shp(4, {{-1.4142135, 0.0, 0},
                        {-0.707106718813, -0.707106718813, 0},
                        {-0.707106718813, 0.707106781187, 0},
                        {0.0, -1.4142135, 0},
                        {0.0, 0.0, 0},
                        {0.0, 1.4142135, 0},
                        {0.707106781187, -0.707106718813, 0},
                        {0.707106781187, 0.707106781187, 0},
                        {1.4142135, 0.0, 0}}),
            };
        }
        // Arch3636（3.6.3.6）完整城市形状表。anchorN：6=六边形锚（L2/L3/L5）、3=三角形锚（L8）。
        {
            auto& a3636 = c.sets[static_cast<size_t>(static_cast<int>(TilingType::Arch3636))];
            a3636.levels = {2.25, 3.0, 5.25, 8.25};
            a3636.shapes = {
                shp(6, {{0.0, 0.0, 0}}),
                shp(6, {{-1.074569936352, 0.0, 0},
                        {0.0, 0.0, 0},
                        {1.074569931824, 0.0, 0}}),
                shp(6, {{0.0, 0.0, 0},
                        {0.537284965912, 0.930604740898, 0},
                        {1.074569931823, 0.0, 0},
                        {1.611854902264, 0.930604740898, 0}}),
                shp(3, {{-1.074569936353, 0.0, 0},
                        {-0.537284965912, -0.930604859102, 0},
                        {-0.537284965912, 0.930604740898, 0},
                        {0.0, 0.0, 0},
                        {0.537284965912, -0.930604859102, 0},
                        {0.537284965912, 0.930604740898, 0},
                        {1.074569931824, 0.0, 0}}),
            };
        }
        // Laves488（对偶4.8.8：等腰直角三角形密铺）完整城市形状表。anchorN=3（三角形）。
        {
            auto& l488 = c.sets[static_cast<size_t>(static_cast<int>(TilingType::Laves488))];
            l488.levels = {2.0, 4.0, 8.0};
            l488.shapes = {
                shp(3, {{0.0, 0.0, 0},
                        {0.666666578457, 0.0, 0}}),
                shp(3, {{-0.666666666667, -1.333333245124, 0},
                        {-0.666666666667, -0.666666666667, 0},
                        {0.0, -1.999999911791, 0},
                        {0.0, 0.0, 0}}),
                shp(3, {{-1.333333333334, 0.0, 0},
                        {-1.333333333334, 1.999999911791, 0},
                        {-0.666666666667, -1.333333245124, 0},
                        {-0.666666666667, -0.666666666667, 0},
                        {-0.666666666667, 0.666666666667, 0},
                        {-0.666666666667, 1.333333245124, 0},
                        {0.0, -1.999999911791, 0},
                        {0.0, 0.0, 0}}),
            };
        }
        // Laves3636（菱形密铺）完整城市形状表。anchorN=4（菱形）。
        {
            auto& l3636 = c.sets[static_cast<size_t>(static_cast<int>(TilingType::Laves3636))];
            l3636.levels = {1.0, 3.0, 5.0, 6.0, 12.0};
            l3636.shapes = {
                shp(4, {{0.0, 0.0, 0}}),
                shp(4, {{-0.805927448868, 0.465302429551, 0},
                        {0.0, 0.0, 0},
                        {0.0, 0.930604740898, 0}}),
                shp(4, {{-0.805927448868, -0.465302429551, 0},
                        {-0.805927448868, 0.465302429551, 0},
                        {0.0, 0.0, 0},
                        {0.805927448867, -0.465302311347, 0},
                        {0.805927448867, 0.465302429551, 0}}),
                shp(4, {{0.0, 0.0, 0},
                        {0.0, 0.930604740898, 0},
                        {0.805927448868, -0.465302429551, 0},
                        {0.805927448868, 1.395907170449, 0},
                        {1.611854897735, 0.0, 0},
                        {1.611854897735, 0.930604859102, 0}}),
                shp(4, {{-0.805927448868, 0.465302429551, 0},
                        {0.0, -0.930604859102, 0},
                        {0.0, 0.0, 0},
                        {0.0, 0.930604740898, 0},
                        {0.0, 1.8612096, 0},
                        {0.805927448868, -0.465302429551, 0},
                        {0.805927448868, 1.395907170449, 0},
                        {1.611854897735, -0.930604740898, 0},
                        {1.611854897735, 0.0, 0},
                        {1.611854897735, 0.930604859102, 0},
                        {1.611854897735, 1.8612096, 0},
                        {2.417782351132, 0.465302429551, 0}}),
            };
        }
    }
    c.tri.shapes = {
        tr({{0, 0, 0}}),  // L1
        // L2：正格 + 同列下方反格（共水平底边 y = r·h；偏移 (0, −2h/3)）
        tr({{0, 0, 0}, {0, -2.0 / 3.0, 1}}),
        // L4：正大三角 [0,2b]×[0,2h] = 正(i,r) + 正(i+1,r) + 反(i,r) + 正(i,r+1)
        tr({{0, 0, 0}, {1, 0, 0}, {0.5, 1.0 / 3.0, 1}, {0.5, 1, 0}}),
        // L6：6 格正六边形（中心 = 锚上尖顶点；左右顶点、上下平边）
        tr({{0, 0, 0}, {0, 4.0 / 3.0, 1}, {0.5, 1, 0}, {-0.5, 1, 0}, {-0.5, 1.0 / 3.0, 1},
            {0.5, 1.0 / 3.0, 1}}),
        // L8：L6 + 顶上正 (0, 2h) + 底下反 (0, −2h/3)（紧贴六边形下平边的同列下方反格）
        tr({{0, 0, 0}, {0, 4.0 / 3.0, 1}, {0.5, 1, 0}, {-0.5, 1, 0}, {-0.5, 1.0 / 3.0, 1},
            {0.5, 1.0 / 3.0, 1}, {0, 2, 0}, {0, -2.0 / 3.0, 1}}),
    };
    // 同步 shapeLevelIndex（默认每个 level 一个形状；未来同级多变体时手动调整）。
    const auto syncShapeLevels = [](Config::City::TilingSet& s) {
        s.shapeLevelIndex.clear();
        for (int i = 0; i < static_cast<int>(s.shapes.size()); ++i)
            s.shapeLevelIndex.push_back(i);
    };
    syncShapeLevels(c.square);
    syncShapeLevels(c.hex);
    syncShapeLevels(c.tri);
    for (auto& s : c.sets)
        if (!s.shapes.empty()) syncShapeLevels(s);
}

// 内置默认势力表（0..8，下标即 id），与翻新计划 §2.7/§2.8 一致。
void initDefaultFactions(std::vector<Config::Faction>& v) {
    v.clear();
    v.resize(static_cast<std::size_t>(kFactionTotal));

    auto& neutral = v[0];
    neutral.id = 0;
    // 双色系统（⑫）：中立灰占位 = 深灰主色 + 浅灰副色（用户定夺）。
    neutral.color = {96, 96, 96};
    neutral.secondary = {191, 191, 191};

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

// ---- 键名校验（2026-08 工程改进）----
// 缺键回退默认是设计，但**打错的键名**同样静默回退——平衡性全在 config.json，
// 一个 typo 就是"改了没生效"且毫无提示。以下在解析后整树校验：
// 已知键集合外的键 → spdlog::warn（不阻断；'_' 前缀键如 _comment 视为注释，不告警）。
// 维护：每增删 config 键，须同步更新下方列表与 toJson（键严格对称，单测往返锁定）。
namespace {

void warnUnknownKeys(const Json& obj, const std::vector<std::string>& known,
                     const std::string& path) {
    if (!obj.is_object()) return;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const std::string& k = it.key();
        if (!k.empty() && k[0] == '_') continue;  // _comment 等注释键
        if (std::find(known.begin(), known.end(), k) == known.end())
            spdlog::warn("config: unknown key '{}' under '{}' (ignored; typo?)", k, path);
    }
}

void validateConfigKeys(const Json& root) {
    using V = std::vector<std::string>;
    // 顶层。
    warnUnknownKeys(root,
                    {"map", "army", "sea", "terrain", "units", "factions", "effect",
                     "projectile", "economy", "city", "capital", "sim", "render", "player",
                     "ui", "tech"},
                    "<root>");
    // 对象取值辅助（不存在/非对象 → 空对象，warnUnknownKeys 自动跳过）。
    auto obj = [&root](const char* name) -> const Json& {
        static const Json kEmpty = Json::object();
        return (root.contains(name) && root[name].is_object()) ? root[name] : kEmpty;
    };
    auto child = [](const Json& parent, const char* name) -> const Json& {
        static const Json kEmpty = Json::object();
        return (parent.contains(name) && parent[name].is_object()) ? parent[name] : kEmpty;
    };
    // 纯键值段。
    warnUnknownKeys(obj("map"),
                    {"file", "width", "height", "blockSize", "panelWidth", "capitalMinDistance",
                     "tiling"},
                    "map");
    warnUnknownKeys(obj("army"),
                    {"baseSpeed", "baseSize", "bounceJitterHalfRange", "bounceJitterDenominator"},
                    "army");
    warnUnknownKeys(obj("sea"),
                    {"initialGoSeaProbability", "goSeaIncrease", "goSeaChanceConst",
                     "goSeaChanceDenominator", "seaSpeedMult"},
                    "sea");
    warnUnknownKeys(obj("terrain"),
                    {"seaChannelMin", "probFloor", "probScale", "mountainEnterChance",
                     "mountainSpeedMult"},
                    "terrain");
    warnUnknownKeys(obj("projectile"), {"drawSize", "mountainLifespanPenalty"}, "projectile");
    warnUnknownKeys(obj("economy"),
                    {"initialEconomy", "perLandIncome", "cityBaseMult", "capitalBase"}, "economy");
    warnUnknownKeys(obj("capital"), {"relocationDelayTicks"}, "capital");
    warnUnknownKeys(obj("sim"), {"tickRate", "maxArmyGuard"}, "sim");
    warnUnknownKeys(obj("player"),
                    {"hoverCityRadius", "markerAlpha", "markerRotateSpeed", "markerRingMargin",
                     "markerArrowGap"},
                    "player");
    warnUnknownKeys(obj("ui"), {"messageMaxShown"}, "ui");
    // effect 嵌套。
    const Json& eff = obj("effect");
    warnUnknownKeys(eff, {"bomb", "mine", "laser"}, "effect");
    warnUnknownKeys(child(eff, "bomb"), {"lifetimeTicks", "conquerEveryTicks", "baseRadius"},
                    "effect.bomb");
    warnUnknownKeys(child(eff, "mine"),
                    {"radius", "armTicks", "checkEveryTicks", "timeoutTicks", "triggerRadiusMult",
                     "triggerBombRadius"},
                    "effect.mine");
    warnUnknownKeys(child(eff, "laser"),
                    {"durationTicks", "length", "extendPerTick", "killExtraRadius",
                     "beamSpreadPIFrac"},
                    "effect.laser");
    // render 嵌套。
    const Json& ren = obj("render");
    warnUnknownKeys(ren,
                    {"windowWidth", "windowHeight", "spriteSize", "armyDrawSize", "city",
                     "capital", "tile"},
                    "render");
    warnUnknownKeys(child(ren, "city"),
                    {"lineMinCellPx", "lineThickness", "lineDarken", "iconDarken", "iconScale",
                     "mix"},
                    "render.city");
    warnUnknownKeys(child(child(ren, "city"), "mix"),
                    {"primary", "secondary", "black"},
                    "render.city.mix");
    warnUnknownKeys(child(ren, "tile"),
                    {"primary", "secondary", "white"},
                    "render.tile");
    warnUnknownKeys(child(ren, "capital"),
                    {"minIconSizePx", "lineThickness", "designatedAlpha", "iconScale"},
                    "render.capital");
    // city（顶层，P13 城市系统 + P12 按密铺形状表）。
    const Json& cityJ = obj("city");
    warnUnknownKeys(cityJ, {"levelIncomeExponent", "levelRankExponent", "shapes", "hex", "tri",
                            "tilings"},
                    "city");
    const V kShapeKeys = {"level", "w", "h"};
    const V kHexTriShapeKeys = {"level", "anchorN", "cells"};
    // 数组段（units / factions 在根；city.shapes 在 city 下）。unitPreference 键为动态
    // 兵种名（normal/vanguard/…）→ 列在已知集合里自然跳过动态校验。
    const V kUnitKeys = {"type",          "cost",              "speedMult",
                         "sizeMult",      "bounceMult",        "visualRadius",
                         "mountainEnterMult", "mountainNoSlow", "deathEffect",
                         "periodic",      "periodTicks",       "bulletSpeed",
                         "bulletSpeedJitter", "bulletCount",   "bulletLifespanTicks",
                         "bulletSize",    "bulletSpreadPIFrac", "bulletSpreadJitterFrac"};
    const V kFactionKeys = {"id", "name", "color", "secondary", "unitPreference", "speedMultAll",
                            "seaMult", "bounceMultAll", "pioneerSpeedMult", "extraLaserBeams",
                            "laserDurationMult", "laserLengthMult", "bombRadius",
                            "mineTriggerRadius", "freeArmyChance"};
    const V kTechKeys = {"id", "name", "desc", "levels", "preferenceUnits"};
    const V kLevelKeys = {"type", "param", "magnitude"};
    auto arrayOf = [&root](const char* name) -> const Json& {
        static const Json kEmpty = Json::array();
        return (root.contains(name) && root[name].is_array()) ? root[name] : kEmpty;
    };
    int idx = 0;
    for (const auto& u : arrayOf("units"))
        warnUnknownKeys(u, kUnitKeys, "units[" + std::to_string(idx++) + "]");
    idx = 0;
    for (const auto& f : arrayOf("factions"))
        warnUnknownKeys(f, kFactionKeys, "factions[" + std::to_string(idx++) + "]");
    idx = 0;
    if (cityJ.contains("shapes") && cityJ["shapes"].is_array()) {
        for (const auto& s : cityJ["shapes"])
            warnUnknownKeys(s, kShapeKeys, "city.shapes[" + std::to_string(idx++) + "]");
    }
    idx = 0;
    if (cityJ.contains("hex") && cityJ["hex"].is_object()) {
        warnUnknownKeys(cityJ["hex"], {"levels", "shapes"}, "city.hex");
        if (cityJ["hex"].contains("shapes") && cityJ["hex"]["shapes"].is_array()) {
            for (const auto& s : cityJ["hex"]["shapes"])
                warnUnknownKeys(s, kHexTriShapeKeys, "city.hex.shapes[" + std::to_string(idx++) + "]");
        }
    }
    idx = 0;
    if (cityJ.contains("tri") && cityJ["tri"].is_object()) {
        warnUnknownKeys(cityJ["tri"], {"levels", "shapes"}, "city.tri");
        if (cityJ["tri"].contains("shapes") && cityJ["tri"]["shapes"].is_array()) {
            for (const auto& s : cityJ["tri"]["shapes"])
                warnUnknownKeys(s, kHexTriShapeKeys, "city.tri.shapes[" + std::to_string(idx++) + "]");
        }
    }
    // 2026-08-16：city.tilings.<tilingName>（半正/Laves 形状表，键动态）。
    if (cityJ.contains("tilings") && cityJ["tilings"].is_object()) {
        for (auto it = cityJ["tilings"].begin(); it != cityJ["tilings"].end(); ++it) {
            const std::string path = "city.tilings." + it.key();
            if (!it.value().is_object()) continue;
            warnUnknownKeys(it.value(), {"levels", "shapes"}, path);
            if (it.value().contains("shapes") && it.value()["shapes"].is_array()) {
                int si = 0;
                for (const auto& s : it.value()["shapes"])
                    warnUnknownKeys(s, kHexTriShapeKeys, path + ".shapes[" + std::to_string(si++) + "]");
            }
        }
    }
    // tech：阈值键 + techs 数组（含 levels）。
    const Json& tech = obj("tech");
    warnUnknownKeys(tech,
                    {"thresholdBase", "thresholdStep", "pointsPerCityLevel",
                     "preferencePerLevel", "playerCandidateCount", "techs"},
                    "tech");
    if (tech.contains("techs") && tech["techs"].is_array()) {
        idx = 0;
        for (const auto& tj : tech["techs"]) {
            warnUnknownKeys(tj, kTechKeys, "tech.techs[" + std::to_string(idx) + "]");
            if (tj.is_object() && tj.contains("levels") && tj["levels"].is_array()) {
                int lv = 0;
                for (const auto& lj : tj["levels"])
                    warnUnknownKeys(lj, kLevelKeys,
                                    "tech.techs[" + std::to_string(idx) + "].levels[" +
                                        std::to_string(lv++) + "]");
            }
            ++idx;
        }
    }
}

}  // namespace

// 默认构造：内置城市等级/形状表（P12 三密铺），保证裸 City{} 即可用
//（Map::cityConfig_、单测直接构造等；Config 自身成员亦由本构造填充）。
Config::City::City() { initDefaultCity(*this); }

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
        // P12：密铺类型（square/hex/tri）。预装 BMP 恒为方形；非方形由随机图生成器产出。
        cfg.map.tiling = getStr(mapJson, "tiling", cfg.map.tiling);
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
            // 双色系统（⑫）：副色（浅灰默认；缺键保持默认）。
            if (factionJson.contains("secondary") && factionJson["secondary"].is_array()
                && factionJson["secondary"].size() == 3) {
                fdef.secondary[0] = factionJson["secondary"][0].get<int>();
                fdef.secondary[1] = factionJson["secondary"][1].get<int>();
                fdef.secondary[2] = factionJson["secondary"][2].get<int>();
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

    // ---- city（P13 城市系统：等级幂律指数 + 按密铺形状表）----
    if (root.contains("city") && root["city"].is_object()) {
        const auto& cityJson = root["city"];
        cfg.city.levelIncomeExponent =
            getNum(cityJson, "levelIncomeExponent", cfg.city.levelIncomeExponent);
        cfg.city.levelRankExponent =
            getNum(cityJson, "levelRankExponent", cfg.city.levelRankExponent);
        // 正方形 shapes 覆盖（可选）：[{level, w, h}, ...]（旧格式）；缺省保持内置表。
        if (cityJson.contains("shapes") && cityJson["shapes"].is_array()) {
            for (const auto& s : cityJson["shapes"]) {
                if (!s.is_object()) continue;
                const double lv = getNum(s, "level", -1.0);
                const int idx = cfg.city.square.levelIndex(lv);
                if (idx < 0) continue;
                const int w = getInt(s, "w", 1);
                const int h = getInt(s, "h", 1);
                Config::City::Shape sh;
                for (int dy = 0; dy < h; ++dy)
                    for (int dx = 0; dx < w; ++dx)
                        sh.cells.push_back({static_cast<double>(dx), static_cast<double>(dy), 0});
                cfg.city.square.shapes[static_cast<size_t>(idx)] = std::move(sh);
            }
        }
        // P12：六/三角形状表（可选）。hex cells = [dq, dr] 轴向偏移；tri cells = [x, y, o]
        // （x 以 b 计、y 以 h 计、o 朝向 0 正/1 反）。均为**配置空间**值（toJson 原样回吐，
        // 往返无损）；世界转换在 Map 放置时经 TilingGeom 完成。
        // 2026-08-16：新密铺 cells 也统一为 [dx, dy, orient]（无朝向时 orient=0）。
        const auto parseTilingSet = [](const Json& sub, Config::City::TilingSet& set) {
            if (sub.contains("levels") && sub["levels"].is_array()) {
                set.levels.clear();
                for (const auto& l : sub["levels"])
                    if (l.is_number()) set.levels.push_back(l.get<double>());
            }
            set.shapes.clear();
            set.shapeLevelIndex.clear();
            if (sub.contains("shapes") && sub["shapes"].is_array()) {
                for (const auto& s : sub["shapes"]) {
                    if (!s.is_object()) continue;
                    const double lv = getNum(s, "level", -1.0);
                    const int idx = set.levelIndex(lv);
                    if (idx < 0 || !s.contains("cells") || !s["cells"].is_array()) continue;
                    Config::City::Shape sh;
                    sh.anchorN = getInt(s, "anchorN", 0);
                    for (const auto& cl : s["cells"]) {
                        if (!cl.is_array() || cl.size() < 2) continue;
                        const int orient = (cl.size() >= 3) ? cl[2].get<int>() : 0;
                        sh.cells.push_back(
                            {cl[0].get<double>(), cl[1].get<double>(), orient});
                    }
                    set.shapes.push_back(std::move(sh));
                    set.shapeLevelIndex.push_back(idx);
                }
            }
        };
        if (cityJson.contains("hex") && cityJson["hex"].is_object())
            parseTilingSet(cityJson["hex"], cfg.city.hex);
        if (cityJson.contains("tri") && cityJson["tri"].is_object())
            parseTilingSet(cityJson["tri"], cfg.city.tri);
        // 2026-08-16：半正/Laves 形状表（可选；city.tilings 下按 tilingName 存）。
        if (cityJson.contains("tilings") && cityJson["tilings"].is_object()) {
            for (auto it = cityJson["tilings"].begin(); it != cityJson["tilings"].end(); ++it) {
                const TilingType tt = tilingFromName(it.key());
                const int idx = static_cast<int>(tt);
                if (idx < 0 || idx >= kTilingTypeCount) continue;
                if (tt == TilingType::Square || tt == TilingType::Hex || tt == TilingType::Tri)
                    continue;  // 具名段已解析，避免重复
                if (!it.value().is_object()) continue;
                parseTilingSet(it.value(), cfg.city.sets[static_cast<size_t>(idx)]);
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
            // 双色系统（⑫）：城市图标 主:副:黑 混合比例（render.city.mix）。
            if (rc.contains("mix") && rc["mix"].is_object()) {
                const auto& mix = rc["mix"];
                cfg.render.city.mix.primary = getNum(mix, "primary", cfg.render.city.mix.primary);
                cfg.render.city.mix.secondary =
                    getNum(mix, "secondary", cfg.render.city.mix.secondary);
                cfg.render.city.mix.black = getNum(mix, "black", cfg.render.city.mix.black);
            }
            // iconScale[]：9 项（下标 = 等级-1，与 data/tower/tower<N>.png 对应）。
            if (rc.contains("iconScale") && rc["iconScale"].is_array())
                for (std::size_t i = 0; i < cfg.render.city.iconScale.size() && i < rc["iconScale"].size();
                     ++i)
                    if (rc["iconScale"][i].is_number())
                        cfg.render.city.iconScale[i] = rc["iconScale"][i].get<double>();
        }
        // 双色系统（⑫）：地块格 主:副:白 混合比例（render.tile）。
        if (renderJson.contains("tile") && renderJson["tile"].is_object()) {
            const auto& tile = renderJson["tile"];
            cfg.render.tileMix.primary = getNum(tile, "primary", cfg.render.tileMix.primary);
            cfg.render.tileMix.secondary = getNum(tile, "secondary", cfg.render.tileMix.secondary);
            cfg.render.tileMix.white = getNum(tile, "white", cfg.render.tileMix.white);
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

    validateConfigKeys(root);  // 键名校验（2026-08）：未知键记警告，不阻断
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
                {"capitalMinDistance", map.capitalMinDistance},
                {"tiling", map.tiling}};

    j["army"] = {{"baseSpeed", army.baseSpeed},
                 {"baseSize", army.baseSize},
                 {"bounceJitterHalfRange", army.bounceJitterHalfRange},
                 {"bounceJitterDenominator", army.bounceJitterDenominator}};

    j["sea"] = {{"initialGoSeaProbability", sea.initialGoSeaProbability},
                {"goSeaIncrease", sea.goSeaIncrease},
                {"goSeaChanceConst", sea.goSeaChanceConst},
                {"goSeaChanceDenominator", sea.goSeaChanceDenominator},
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
        fj["secondary"] = {f.secondary[0], f.secondary[1], f.secondary[2]};
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
    for (int i = 0; i < static_cast<int>(city.square.levels.size()); ++i) {
        // 正方形：{level, w, h}（旧格式；w/h = 形状 AABB）。
        int w = 0, h = 0;
        for (const auto& c : city.square.shapes[static_cast<size_t>(i)].cells) {
            w = std::max(w, static_cast<int>(c.dx) + 1);
            h = std::max(h, static_cast<int>(c.dy) + 1);
        }
        shapesJ.push_back({{"level", city.square.levels[static_cast<size_t>(i)]}, {"w", w},
                           {"h", h}});
    }
    // P12：六/三角形状表（cells 原样回吐配置空间值 → 往返无损）。
    const auto tilingSetJ = [](const Config::City::TilingSet& set) {
        Json shapes = Json::array();
        for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
            Json cells = Json::array();
            for (const auto& c : set.shapes[static_cast<size_t>(s)].cells)
                cells.push_back({c.dx, c.dy, c.orient});
            const int li = (s < static_cast<int>(set.shapeLevelIndex.size()))
                               ? set.shapeLevelIndex[static_cast<size_t>(s)]
                               : 0;
            shapes.push_back({{"level", set.levels[static_cast<size_t>(li)]},
                              {"anchorN", set.shapes[static_cast<size_t>(s)].anchorN},
                              {"cells", cells}});
        }
        return Json{{"levels", set.levels}, {"shapes", std::move(shapes)}};
    };
    // 2026-08-16：半正/Laves 形状表统一放 city.tilings.<tilingName>；缺省为空集。
    Json tilingsJ = Json::object();
    for (int i = 0; i < kTilingTypeCount; ++i) {
        const TilingType tt = static_cast<TilingType>(i);
        if (tt == TilingType::Square || tt == TilingType::Hex || tt == TilingType::Tri) continue;
        tilingsJ[tilingName(tt)] = tilingSetJ(city.sets[static_cast<size_t>(i)]);
    }
    j["city"] = {{"levelIncomeExponent", city.levelIncomeExponent},
                 {"levelRankExponent", city.levelRankExponent},
                 {"shapes", std::move(shapesJ)},
                 {"hex", tilingSetJ(city.hex)},
                 {"tri", tilingSetJ(city.tri)},
                 {"tilings", std::move(tilingsJ)}};

    j["capital"] = {{"relocationDelayTicks", capital.relocationDelayTicks}};

    j["sim"] = {{"tickRate", sim.tickRate}, {"maxArmyGuard", sim.maxArmyGuard}};

    // P13 城市渲染常数：iconScale[]（9 项，下标 = 等级-1，与 data/tower/tower<N>.png 对应）。
    j["render"] = {
        {"windowWidth", render.windowWidth},
        {"windowHeight", render.windowHeight},
        {"spriteSize", render.spriteSize},
        {"armyDrawSize", render.armyDrawSize},
        // 双色系统（⑫）：地块格 主:副:白 混合比例。
        {"tile",
         {{"primary", render.tileMix.primary},
          {"secondary", render.tileMix.secondary},
          {"white", render.tileMix.white}}},
        {"city",
         {{"lineMinCellPx", render.city.lineMinCellPx},
          {"lineThickness", render.city.lineThickness},
          {"lineDarken", render.city.lineDarken},
          {"iconDarken", render.city.iconDarken},
          // 双色系统（⑫）：城市图标 主:副:黑 混合比例。
          {"mix",
           {{"primary", render.city.mix.primary},
            {"secondary", render.city.mix.secondary},
            {"black", render.city.mix.black}}},
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

std::array<int, 3> Config::factionTileColor(int id) const {
    // 地块格填色 = 主色:副色:白 加权平均（render.tileMix；⑫）。id 越界 → 中性灰占位。
    if (id < 0 || id >= static_cast<int>(factions.size()))
        return weightedMix3({96, 96, 96}, {191, 191, 191}, {255, 255, 255},
                            render.tileMix.primary, render.tileMix.secondary,
                            render.tileMix.white);
    const auto& f = factions[static_cast<size_t>(id)];
    return weightedMix3(f.color, f.secondary, {255, 255, 255}, render.tileMix.primary,
                        render.tileMix.secondary, render.tileMix.white);
}

std::array<int, 3> Config::factionCityColor(int id) const {
    // 城市图标填色 = 主色:副色:黑 加权平均（render.city.mix；⑫）。id 越界 → 中性灰占位。
    if (id < 0 || id >= static_cast<int>(factions.size()))
        return weightedMix3({96, 96, 96}, {191, 191, 191}, {0, 0, 0},
                            render.city.mix.primary, render.city.mix.secondary,
                            render.city.mix.black);
    const auto& f = factions[static_cast<size_t>(id)];
    return weightedMix3(f.color, f.secondary, {0, 0, 0}, render.city.mix.primary,
                        render.city.mix.secondary, render.city.mix.black);
}

}  // namespace lw
