#include "core/Config.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Paths.h"
#include "world/tiling/Tiling.h"

namespace lw {

namespace {

using Json = nlohmann::json;

// 数值读取辅助：缺键/类型不符时返回默认值。
double getNum(const Json& j, const char* key, double def) {
    if (j.contains(key)) {
        if (!j[key].is_number()) throw std::runtime_error(std::string(key) + " must be a number");
        return j[key].get<double>();
    }
    return def;
}
int getInt(const Json& j, const char* key, int def) {
    if (j.contains(key)) {
        if (!j[key].is_number_integer())
            throw std::runtime_error(std::string(key) + " must be an integer");
        return j[key].get<int>();
    }
    return def;
}
std::string getStr(const Json& j, const char* key, const std::string& def) {
    if (j.contains(key)) {
        if (!j[key].is_string()) throw std::runtime_error(std::string(key) + " must be a string");
        return j[key].get<std::string>();
    }
    return def;
}
bool getBool(const Json& j, const char* key, bool def) {
    if (j.contains(key)) {
        if (!j[key].is_boolean()) throw std::runtime_error(std::string(key) + " must be boolean");
        return j[key].get<bool>();
    }
    return def;
}

bool readJsonFile(const std::string& path, Json& out, const char* kind) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        spdlog::warn("{} file '{}' not found", kind, path);
        return false;
    }
    try {
        ifs >> out;
    } catch (const std::exception& e) {
        spdlog::warn("{} file '{}' parse failed: {}", kind, path, e.what());
        return false;
    }
    if (!out.is_object()) {
        spdlog::warn("{} file '{}' must contain a JSON object", kind, path);
        return false;
    }
    return true;
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

// 掩码 → 允许的基础格编号（升序），供 toJson 输出 anchorBases 数组。
std::vector<int> anchorMaskToBases(std::uint32_t v) {
    std::vector<int> bases;
    for (int b = 0; b < 32; ++b)
        if ((v >> b) & 1u) bases.push_back(b);
    return bases;
}

// 解析单个密铺的等级/形状集（data/city_shapes.json 与 config.json 的
// city.hex/tri/tilings 共用；shapes[].level → shapeLevelIndex 容差匹配推导）。
void parseTilingSetJson(const Json& sub, Config::City::TilingSet& set) {
    if (sub.contains("levels") && sub["levels"].is_array()) {
        set.levels.clear();
        for (const auto& l : sub["levels"])
            if (l.is_number()) set.levels.push_back(l.get<double>());
    }
    set.shapes.clear();
    set.shapeLevelIndex.clear();
    // baseGroups：几何完全一致的基础格归组（组名任意，仅在本密铺内有效）。
    // 形状的 anchorBases 引用组名（或直接写基础格编号），运行时展开为位掩码。
    std::map<std::string, std::uint32_t> baseGroups;
    if (sub.contains("baseGroups") && sub["baseGroups"].is_object()) {
        for (auto it = sub["baseGroups"].begin(); it != sub["baseGroups"].end(); ++it) {
            std::uint32_t m = 0;
            if (it.value().is_array()) {
                for (const auto& bv : it.value()) {
                    if (!bv.is_number()) continue;
                    const int b = bv.get<int>();
                    if (b >= 0 && b < 32) m |= (1u << b);
                }
            }
            baseGroups[it.key()] = m;
        }
    }
    if (sub.contains("shapes") && sub["shapes"].is_array()) {
        for (const auto& s : sub["shapes"]) {
            if (!s.is_object()) continue;
            if (getBool(s, "disabled", false)) continue;  // disabled: true → 读入时忽略该形状
            const double lv = getNum(s, "level", -1.0);
            const int idx = set.levelIndex(lv);
            if (idx < 0 || !s.contains("cells") || !s["cells"].is_array()) continue;
            Config::City::Shape sh;
            if (s.contains("anchorBases") && s["anchorBases"].is_array()) {
                for (const auto& entry : s["anchorBases"]) {
                    if (entry.is_string()) {
                        const auto git = baseGroups.find(entry.get<std::string>());
                        if (git != baseGroups.end()) {
                            sh.anchorBaseMask |= git->second;
                        } else {
                            spdlog::warn("city shapes: unknown anchorBases group '{}' (ignored)",
                                         entry.get<std::string>());
                        }
                    } else if (entry.is_number()) {
                        const int b = entry.get<int>();
                        if (b >= 0 && b < 32) sh.anchorBaseMask |= (1u << b);
                    }
                }
            }
            for (const auto& cl : s["cells"]) {
                if (!cl.is_array() || cl.size() < 2) continue;
                sh.cells.push_back({cl[0].get<double>(), cl[1].get<double>()});
            }
            set.shapes.push_back(std::move(sh));
            set.shapeLevelIndex.push_back(idx);
        }
    }
    // 贴图等级显式映射（可选；空或缺省 = lround 推导）。
    set.iconLevels.clear();
    if (sub.contains("iconLevels") && sub["iconLevels"].is_array()) {
        for (const auto& iv : sub["iconLevels"])
            if (iv.is_number()) set.iconLevels.push_back(iv.get<int>());
    }
}

// 加载 data/city_shapes.json：全部密铺（square/hex/tri + 半正/Laves）的城市形状表
//（键 = tilingName，如 "square"、"hex"、"arch_3464"）。文件缺失/损坏时保留代码内置兜底，
// 仅记警告不阻断。
void loadCityShapesFile(Config::City& c) {
    std::ifstream ifs(kCityShapesPath);
    if (!ifs.is_open()) {
        spdlog::warn("city shapes file '{}' not found; keeping built-in fallback",
                     kCityShapesPath);
        return;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    Json root;
    try {
        root = Json::parse(oss.str());
    } catch (const std::exception& e) {
        spdlog::warn("city shapes file '{}' parse failed: {}", kCityShapesPath, e.what());
        return;
    }
    if (!root.is_object()) return;
    for (auto it = root.begin(); it != root.end(); ++it) {
        const TilingType tt = tilingFromName(it.key());
        const int idx = static_cast<int>(tt);
        if (idx < 0 || idx >= kTilingTypeCount) continue;
        if (!it.value().is_object()) continue;
        switch (tt) {
            case TilingType::Square:
                parseTilingSetJson(it.value(), c.square);
                break;
            case TilingType::Hex:
                parseTilingSetJson(it.value(), c.hex);
                break;
            case TilingType::Tri:
                parseTilingSetJson(it.value(), c.tri);
                break;
            default:
                parseTilingSetJson(it.value(), c.sets[static_cast<size_t>(idx)]);
                break;
        }
    }
}

// 内置默认城市表（P13 + P12）：等级/形状按密铺。
// ShapeCell 偏移语义（Config.h 注）：全部为相对锚格中心的**世界单位偏移**
// （方/六/三/半正/Laves 统一；三角正锚模式，反锚由 Map 解析端镜像 dy）。
void initDefaultCity(Config::City& c) {
    using Shape = Config::City::Shape;
    using ShapeCell = Config::City::ShapeCell;
    const auto sq = [](int w, int h) {
        Shape sh;
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx < w; ++dx)
                sh.cells.push_back(ShapeCell{static_cast<double>(dx), static_cast<double>(dy)});
        return sh;
    };
    const auto hx = [](std::initializer_list<ShapeCell> list) {
        Shape sh;
        sh.cells.assign(list.begin(), list.end());
        return sh;
    };
    const auto tr = [](std::initializer_list<ShapeCell> list) {
        Shape sh;
        sh.cells.assign(list.begin(), list.end());
        return sh;
    };
    // 正方形：1/2/4/6/9（旧 w×h：1×1 / 1×2 / 2×2 / 2×3 / 3×3）。
    c.square.levels = {1, 2, 4, 6, 9};
    c.square.shapes = {sq(1, 1), sq(1, 2), sq(2, 2), sq(2, 3), sq(3, 3)};
    // 六边形：1/3/4/6/7/9（世界偏移已由旧轴向表换算，P12 §3.2 定稿；锚 = (0,0)）。
    c.hex.levels = {1, 3, 4, 6, 7, 9};
    c.hex.shapes = {
        hx({{0.0, 0.0}}),
        hx({{0.0, 0.0}, {-0.5372849659117709, -0.9306048591020997},
            {0.5372849659117709, -0.9306048591020997}}),
        hx({{0.0, 0.0}, {-0.5372849659117709, -0.9306048591020997},
            {0.5372849659117709, -0.9306048591020997}, {0.0, -1.8612097182041993}}),
        hx({{0.0, 0.0}, {-0.5372849659117709, -0.9306048591020997},
            {0.5372849659117709, -0.9306048591020997}, {0.0, -1.8612097182041993},
            {-1.0745699318235418, -1.8612097182041993}, {1.0745699318235418, -1.8612097182041993}}),
        hx({{0.0, 0.0}, {1.0745699318235418, 0.0}, {-1.0745699318235418, 0.0},
            {0.5372849659117709, 0.9306048591020997}, {-0.5372849659117709, -0.9306048591020997},
            {0.5372849659117709, -0.9306048591020997}, {-0.5372849659117709, 0.9306048591020997}}),
        hx({{0.0, 0.0}, {1.0745699318235418, 0.0}, {-1.0745699318235418, 0.0},
            {0.5372849659117709, 0.9306048591020997}, {-0.5372849659117709, -0.9306048591020997},
            {0.5372849659117709, -0.9306048591020997}, {-0.5372849659117709, 0.9306048591020997},
            {0.0, 1.8612097182041993}, {0.0, -1.8612097182041993}}),
    };
    // 三角形：1/2/4/6/8（世界偏移以 kTriSide/kTriAlt 换算；**正锚模式**——表按"锚 =
    // 正三角"定义；锚为反三角时解析端对 dy 垂直镜像，即该等级的朝向变体）。
    // 语义（用户 2026-08 定稿）：
    //   L1 = 1 格（锚正→正、锚反→反，2 种模式）
    //   L2 = 一正一反，公共边**水平**（正底边 = 反底边，同列上下相邻）
    //   L4 = 边长 2b 大三角含 4 小三角（锚正→尖朝上、锚反→尖朝下，2 种模式）
    //   L6 = 6 格正六边形（左右顶点、上下平边；3 正 3 反；锚镜像同形，1 种模式）
    //   L8 = L6 + 顶上正 + 底下反（≡ 两个 4 级城拼接；锚镜像旋转 180° 同构，1 种模式）
    c.tri.levels = {1, 2, 4, 6, 8};
    c.tri.shapes = {
        tr({{0.0, 0.0}}),  // L1
        // L2：正格 + 同列下方反格（共水平底边 y = r·h；偏移 (0, −2h/3)）
        tr({{0.0, 0.0}, {0.0, -0.8773826753016616}}),
        // L4：正大三角 [0,2b]×[0,2h] = 正(i,r) + 正(i+1,r) + 反(i,r) + 正(i,r+1)
        tr({{0.0, 0.0}, {1.5196713713031924, 0.0}, {0.7598356856515962, 0.4386913376508308},
               {0.7598356856515962, 1.3160740129524924}}),
        // L6：6 格正六边形（中心 = 锚上尖顶点；左右顶点、上下平边）
        tr({{0.0, 0.0}, {0.0, 1.7547653506033232}, {0.7598356856515962, 1.3160740129524924},
               {-0.7598356856515962, 1.3160740129524924}, {-0.7598356856515962, 0.4386913376508308},
               {0.7598356856515962, 0.4386913376508308}}),
        // L8：L6 + 顶上正 (0, 2h) + 底下反 (0, −2h/3)（紧贴六边形下平边的同列下方反格）
        tr({{0.0, 0.0}, {0.0, 1.7547653506033232}, {0.7598356856515962, 1.3160740129524924},
               {-0.7598356856515962, 1.3160740129524924}, {-0.7598356856515962, 0.4386913376508308},
               {0.7598356856515962, 0.4386913376508308}, {0.0, 2.6321480259049848},
               {0.0, -0.8773826753016616}}),
    };
    // 2026-08-16：先给 1 级城（单格）兜底占位；下面每个已接入几何的密铺都会
    // 用完整等级形状表整体覆盖（开发思路.txt 没提 1 级城的密铺不会被覆盖成 1 级）。
    {
        const auto single = [] {
            Shape sh;
            sh.cells.push_back(ShapeCell{0.0, 0.0});
            return sh;
        };
        for (const TilingType t :
             {TilingType::Arch33336, TilingType::Arch33434, TilingType::Arch3464,
              TilingType::Arch3636, TilingType::Arch31212, TilingType::Arch4612,
              TilingType::Arch488, TilingType::Laves3636, TilingType::Laves31212,
              TilingType::Laves4612, TilingType::Laves488, TilingType::Laves33434,
              TilingType::Laves33336, TilingType::Laves3464}) {
            auto& set = c.sets[static_cast<size_t>(static_cast<int>(t))];
            set.levels = {1.0};
            set.shapes = {single()};
        }
        // 完整城市形状表已外置到 data/city_shapes.json（2026-08-26；由
        // tools/gen_city_shapes_json.py 生成，改表可重跑该工具或直接编辑 JSON）。
        // 文件缺失/损坏时保留上面的兜底（仅记警告，不阻断）。
        loadCityShapesFile(c);
    }
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
        if (!s.shapes.empty() && s.shapeLevelIndex.empty()) syncShapeLevels(s);
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
    // P1.1（2026-08）：map.file/width/height 已移除（地图在菜单选择）；键表不再列出，
    // 出现即视为未知键告警。
    warnUnknownKeys(obj("map"),
                    {"blockSize", "panelWidth", "capitalMinDistance",
                     "cityMountainWeight", "tiling"},
                    "map");
    warnUnknownKeys(obj("army"), {"baseSpeed", "baseSize", "bounceJitterRangeRad"}, "army");
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
                    {"lineMinCellPx", "lineThickness", "lineDarken", "iconDarken", "mix"},
                    "render.city");
    warnUnknownKeys(child(child(ren, "city"), "mix"),
                    {"primary", "secondary", "black"},
                    "render.city.mix");
    warnUnknownKeys(child(ren, "tile"),
                    {"primary", "secondary", "white", "variation"},
                    "render.tile");
    warnUnknownKeys(child(ren, "capital"),
                    {"minIconSizePx", "lineThickness", "designatedAlpha"},
                    "render.capital");
    // city（顶层，P13 城市系统 + P12 按密铺形状表）。
    const Json& cityJ = obj("city");
    warnUnknownKeys(cityJ, {"levelIncomeExponent", "levelRankExponent", "shapes", "hex", "tri",
                            "tilings"},
                    "city");
    // P1.2：city.shapes 已改为泛化 cells 格式，旧 w/h / anchorBaseMask 不再兼容。
    const V kShapeKeys = {"level", "anchorBases", "cells", "disabled"};
    const V kHexTriShapeKeys = {"level", "anchorBases", "cells", "disabled"};
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
        warnUnknownKeys(cityJ["hex"], {"levels", "shapes", "baseGroups", "iconLevels"}, "city.hex");
        if (cityJ["hex"].contains("shapes") && cityJ["hex"]["shapes"].is_array()) {
            for (const auto& s : cityJ["hex"]["shapes"])
                warnUnknownKeys(s, kHexTriShapeKeys, "city.hex.shapes[" + std::to_string(idx++) + "]");
        }
    }
    idx = 0;
    if (cityJ.contains("tri") && cityJ["tri"].is_object()) {
        warnUnknownKeys(cityJ["tri"], {"levels", "shapes", "baseGroups", "iconLevels"}, "city.tri");
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
            warnUnknownKeys(it.value(), {"levels", "shapes", "baseGroups", "iconLevels"}, path);
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

// 默认构造：内置全部密铺等级/形状表（P1.2 起 square/hex/tri/半正/Laves 统一），
// 保证裸 City{} 即可用（Map::cityConfig_、单测直接构造等；Config 自身成员亦由本构造填充）。
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

    try {
    // ---- map ----
    // P1.1（2026-08）：map.file/width/height 已从 config 移除——地图在菜单界面手动选择，
    // 运行时由 Options/Application/Headless 注入；config 不再读取这三个键（缺键回退默认）。
    if (root.contains("map") && root["map"].is_object()) {
        const auto& mapJson = root["map"];
        cfg.map.blockSize = getInt(mapJson, "blockSize", cfg.map.blockSize);
        cfg.map.panelWidth = getInt(mapJson, "panelWidth", cfg.map.panelWidth);
        cfg.map.capitalMinDistance =
            getInt(mapJson, "capitalMinDistance", cfg.map.capitalMinDistance);
        cfg.map.cityMountainWeight =
            getNum(mapJson, "cityMountainWeight", cfg.map.cityMountainWeight);
        // P12：密铺类型（square/hex/tri）。预装 BMP 恒为方形；非方形由随机图生成器产出。
        cfg.map.tiling = getStr(mapJson, "tiling", cfg.map.tiling);
    }

    // ---- army ----
    if (root.contains("army") && root["army"].is_object()) {
        const auto& armyJson = root["army"];
        cfg.army.baseSpeed = getNum(armyJson, "baseSpeed", cfg.army.baseSpeed);
        cfg.army.baseSize = getNum(armyJson, "baseSize", cfg.army.baseSize);
        cfg.army.bounceJitterRangeRad = getNum(armyJson, "bounceJitterRangeRad",
                                               cfg.army.bounceJitterRangeRad);
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
        // 正方形 shapes 覆盖（可选）：P1.2 起仅支持泛化 [{level, cells:[dx,dy], ...}]，
        // 不再兼容旧 [{level, w, h}]。缺省保持内置表/data/city_shapes.json。
        if (cityJson.contains("shapes") && cityJson["shapes"].is_array()) {
            Json squareObj;
            squareObj["levels"] = cfg.city.square.levels;
            squareObj["shapes"] = cityJson["shapes"];
            parseTilingSetJson(squareObj, cfg.city.square);
        }
        // P12：六/三角形状表（可选）。P1.2 起 cells 统一为 [dx, dy] 世界偏移；
        // 解析逻辑 = parseTilingSetJson（匿名命名空间，与 data/city_shapes.json 共用）。
        if (cityJson.contains("hex") && cityJson["hex"].is_object())
            parseTilingSetJson(cityJson["hex"], cfg.city.hex);
        if (cityJson.contains("tri") && cityJson["tri"].is_object())
            parseTilingSetJson(cityJson["tri"], cfg.city.tri);
        // 2026-08-16：半正/Laves 形状表（可选；city.tilings 下按 tilingName 存）。
        if (cityJson.contains("tilings") && cityJson["tilings"].is_object()) {
            for (auto it = cityJson["tilings"].begin(); it != cityJson["tilings"].end(); ++it) {
                const TilingType tt = tilingFromName(it.key());
                const int idx = static_cast<int>(tt);
                if (idx < 0 || idx >= kTilingTypeCount) continue;
                if (tt == TilingType::Square || tt == TilingType::Hex || tt == TilingType::Tri)
                    continue;  // 具名段已解析，避免重复
                if (!it.value().is_object()) continue;
                parseTilingSetJson(it.value(), cfg.city.sets[static_cast<size_t>(idx)]);
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
        // P13 城市渲染常数（lineMinCellPx/lineThickness/lineDarken/iconDarken/mix；P1.2 删 iconScale）。
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
            // iconFitScale / iconFitOffsetY：已外置到 data/city_icon_fits.json（2026-08-26），
            // 由 loadCityIconFits 加载，不再读 config.json。
        }
        // 双色系统（⑫）：地块格 主:副:白 混合比例（render.tile）。
        if (renderJson.contains("tile") && renderJson["tile"].is_object()) {
            const auto& tile = renderJson["tile"];
            cfg.render.tileMix.primary = getNum(tile, "primary", cfg.render.tileMix.primary);
            cfg.render.tileMix.secondary = getNum(tile, "secondary", cfg.render.tileMix.secondary);
            cfg.render.tileMix.white = getNum(tile, "white", cfg.render.tileMix.white);
            cfg.render.tileMix.variation = getNum(tile, "variation", cfg.render.tileMix.variation);
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
    std::string validationError;
    if (!cfg.validate(&validationError)) {
        spdlog::error("config validation failed: {}; using built-in defaults", validationError);
        Config fallback;
        initDefaultFactions(fallback.factions);
        return fallback;
    }
    return cfg;
    } catch (const std::exception& e) {
        spdlog::error("config load failed: {}; using built-in defaults", e.what());
        Config fallback;
        initDefaultFactions(fallback.factions);
        return fallback;
    }
}

Config Config::loadFromFile(const std::string& path) {
    Json merged;
    if (!readJsonFile(path, merged, "config")) {
        spdlog::warn("config file '{}' unavailable, using built-in defaults", path);
        Config cfg;
        initDefaultFactions(cfg.factions);
        return cfg;
    }

    // config.json 只保留核心/模拟配置；同目录的四个片段按固定顺序覆盖对应顶层段。
    // 片段使用 {"render": {...}} 这类带段名的对象，便于单独校验和人工编辑。
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    const auto fragmentPath = [&parent](const char* name) {
        return (parent / name).string();
    };
    const std::array<std::pair<const char*, const char*>, 4> fragments = {{
        {"render", "render.json"},
        {"tech", "techs.json"},
        {"factions", "factions.json"},
        {"units", "units.json"},
    }};
    for (const auto& [section, file] : fragments) {
        Json fragment;
        const std::string fragmentName = fragmentPath(file);
        if (!readJsonFile(fragmentName, fragment, "config fragment")) continue;
        if (!fragment.contains(section)) {
            spdlog::warn("config fragment '{}' has no '{}' section; ignored", fragmentName, section);
            continue;
        }
        for (auto it = fragment.begin(); it != fragment.end(); ++it) {
            if (!it.key().empty() && it.key()[0] == '_') continue;
            if (it.key() != section)
                spdlog::warn("config fragment '{}' has unexpected top-level key '{}' (ignored)",
                             fragmentName, it.key());
        }
        merged[section] = fragment[section];
    }

    Config cfg = loadFromJson(merged.dump());
    loadCityIconFits(cfg);  // 城市贴图缩放/平移表外置于 data/city_icon_fits.json（2026-08-26）
    return cfg;
}

// 加载外置城市贴图预计算表 data/city_icon_fits.json（render.city.iconFitScale + iconFitOffsetY）。
// 文件缺失/损坏 → 保留空表（任何密铺缺 fit 时按 1 倍缩放兜底，P1.2/P1.3）。
// 注意：config.json 不再含这两块；本函数仅经 loadFromFile 与交互工具调用。
void Config::loadCityIconFits(Config& c) {
    std::ifstream ifs(kCityIconFitsPath);
    if (!ifs.is_open()) {
        spdlog::warn("city icon fits file '{}' not found; iconFitScale/iconFitOffsetY empty",
                     kCityIconFitsPath);
        return;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    Json root;
    try {
        root = Json::parse(oss.str());
    } catch (const std::exception& e) {
        spdlog::warn("city icon fits file '{}' parse failed: {}", kCityIconFitsPath, e.what());
        return;
    }
    if (!root.is_object()) return;
    if (root.contains("iconFitScale") && root["iconFitScale"].is_object()) {
        c.render.city.iconFitScale.clear();
        for (auto it = root["iconFitScale"].begin(); it != root["iconFitScale"].end(); ++it) {
            if (!it.value().is_object()) continue;
            for (auto lv = it.value().begin(); lv != it.value().end(); ++lv) {
                if (!lv.value().is_number()) continue;
                int level = 0;
                try {
                    level = std::stoi(lv.key());
                } catch (...) {
                    continue;
                }
                if (level < 1 || level > 10) continue;
                c.render.city.iconFitScale[it.key()][level] = lv.value().get<double>();
            }
        }
    }
    if (root.contains("iconFitOffsetY") && root["iconFitOffsetY"].is_object()) {
        c.render.city.iconFitOffsetY.clear();
        for (auto it = root["iconFitOffsetY"].begin(); it != root["iconFitOffsetY"].end(); ++it) {
            if (!it.value().is_array()) continue;
            for (const auto& rec : it.value()) {
                if (!rec.is_object()) continue;
                Config::Render::City::IconFitOffsetY o;
                o.iconLevel = getInt(rec, "iconLevel", 1);
                o.variant = getInt(rec, "variant", 0);
                o.value = getNum(rec, "value", 0.0);
                if (rec.contains("bases") && rec["bases"].is_array())
                    for (const auto& b : rec["bases"])
                        if (b.is_number()) o.bases.push_back(b.get<int>());
                c.render.city.iconFitOffsetY[it.key()].push_back(std::move(o));
            }
        }
    }
}

bool Config::validate(std::string* err) const {
    const auto fail = [err](const std::string& message) {
        if (err) *err = message;
        return false;
    };
    const auto finite = [](double value) { return std::isfinite(value); };
    const auto positive = [&](double value) { return finite(value) && value > 0.0; };
    const auto nonNegative = [&](double value) { return finite(value) && value >= 0.0; };
    const auto unitInterval = [&](double value) {
        return finite(value) && value >= 0.0 && value <= 1.0;
    };

    if (map.width <= 0 || map.height <= 0 || map.blockSize <= 0 || map.panelWidth < 0
        || map.capitalMinDistance < 0 || !nonNegative(map.cityMountainWeight))
        return fail("map dimensions or numeric range invalid");
    bool validTiling = false;
    for (int i = 0; i < kTilingTypeCount; ++i)
        if (map.tiling == tilingName(static_cast<TilingType>(i))) validTiling = true;
    if (!validTiling) return fail("map.tiling is unknown");

    if (!positive(army.baseSpeed) || !positive(army.baseSize)
        || !nonNegative(army.bounceJitterRangeRad))
        return fail("army numeric range invalid");
    if (!unitInterval(sea.initialGoSeaProbability) || !nonNegative(sea.goSeaIncrease)
        || !nonNegative(sea.goSeaChanceConst) || !positive(sea.goSeaChanceDenominator)
        || !positive(sea.seaSpeedMult))
        return fail("sea numeric range invalid");
    if (terrain.seaChannelMin < 0 || terrain.seaChannelMin > 255 || terrain.probFloor < 0
        || terrain.probFloor > 255 || terrain.probScale <= 0
        || !unitInterval(terrain.mountainEnterChance) || !positive(terrain.mountainSpeedMult))
        return fail("terrain numeric range invalid");

    for (const auto& u : units) {
        if (!positive(u.cost) || !positive(u.speedMult) || !positive(u.sizeMult)
            || !nonNegative(u.bounceMult) || !positive(u.visualRadius)
            || !nonNegative(u.mountainEnterMult) || u.periodTicks < 0
            || !nonNegative(u.bulletSpeed) || !nonNegative(u.bulletSpeedJitter)
            || u.bulletCount <= 0 || u.bulletLifespanTicks < 0 || !positive(u.bulletSize)
            || !nonNegative(u.bulletSpreadPIFrac) || !nonNegative(u.bulletSpreadJitterFrac))
            return fail("unit numeric range invalid");
        if (u.periodic != PeriodicAction::none && u.periodTicks <= 0)
            return fail("periodic unit must have positive periodTicks");
        if (u.periodic != PeriodicAction::none && u.bulletLifespanTicks <= 0)
            return fail("periodic unit must have positive bullet lifespan");
    }

    if (factions.size() != static_cast<size_t>(kFactionTotal))
        return fail("factions size mismatch");
    for (int i = 0; i < kFactionTotal; ++i) {
        const auto& f = factions[static_cast<size_t>(i)];
        if (f.id != i) return fail("faction ids must match their indexes");
        for (int channel : f.color)
            if (channel < 0 || channel > 255) return fail("faction color out of range");
        for (int channel : f.secondary)
            if (channel < 0 || channel > 255) return fail("faction secondary color out of range");
        for (double preference : f.unitPreference)
            if (!positive(preference)) return fail("unit preference must be positive");
        if (!positive(f.speedMultAll) || !positive(f.seaMult) || !nonNegative(f.bounceMultAll)
            || !positive(f.pioneerSpeedMult) || f.extraLaserBeams < 0
            || !positive(f.laserDurationMult) || !positive(f.laserLengthMult)
            || !positive(f.bombRadius) || !positive(f.mineTriggerRadius)
            || !unitInterval(f.freeArmyChance))
            return fail("faction numeric range invalid");
    }

    if (effect.bomb.lifetimeTicks <= 0 || effect.bomb.conquerEveryTicks <= 0
        || !positive(effect.bomb.baseRadius) || !positive(effect.mine.radius)
        || effect.mine.armTicks < 0 || effect.mine.checkEveryTicks <= 0
        || effect.mine.timeoutTicks <= 0 || !positive(effect.mine.triggerRadiusMult)
        || !positive(effect.mine.triggerBombRadius) || effect.laser.durationTicks <= 0
        || !positive(effect.laser.length) || effect.laser.extendPerTick < 0
        || !nonNegative(effect.laser.killExtraRadius) || !nonNegative(effect.laser.beamSpreadPIFrac))
        return fail("effect numeric range invalid");
    if (projectile.drawSize <= 0 || projectile.mountainLifespanPenalty < 0)
        return fail("projectile numeric range invalid");
    if (!nonNegative(economy.initialEconomy) || !nonNegative(economy.perLandIncome)
        || !nonNegative(economy.cityBaseMult) || !finite(economy.capitalBase))
        return fail("economy numeric range invalid");
    if (!nonNegative(capital.relocationDelayTicks) || !positive(sim.tickRate)
        || sim.maxArmyGuard <= 0)
        return fail("capital or simulation numeric range invalid");
    if (render.windowWidth <= 0 || render.windowHeight <= 0 || render.spriteSize <= 0
        || render.armyDrawSize <= 0 || !finite(render.tileMix.primary)
        || !finite(render.tileMix.secondary) || !finite(render.tileMix.white)
        || !nonNegative(render.tileMix.variation) || render.tileMix.primary < 0.0
        || render.tileMix.secondary < 0.0 || render.tileMix.white < 0.0
        || !positive(render.city.lineMinCellPx) || !positive(render.city.lineThickness)
        || !unitInterval(render.city.lineDarken) || !unitInterval(render.city.iconDarken)
        || !unitInterval(render.city.mix.primary) || !unitInterval(render.city.mix.secondary)
        || !unitInterval(render.city.mix.black) || render.capital.minIconSizePx <= 0
        || !positive(render.capital.lineThickness) || !unitInterval(render.capital.designatedAlpha))
        return fail("render numeric range invalid");
    if (!positive(player.hoverCityRadius) || !unitInterval(player.markerAlpha)
        || !finite(player.markerRotateSpeed) || !nonNegative(player.markerRingMargin)
        || !nonNegative(player.markerArrowGap) || ui.messageMaxShown <= 0)
        return fail("player or ui numeric range invalid");

    const auto validateSet = [&](const City::TilingSet& set, const char* name) {
        if (set.levels.empty() || set.shapeLevelIndex.size() != set.shapes.size())
            return fail(std::string(name) + " shape table is incomplete");
        for (double level : set.levels)
            if (!positive(level)) return fail(std::string(name) + " level is invalid");
        for (size_t i = 0; i < set.shapes.size(); ++i) {
            const int li = set.shapeLevelIndex[i];
            if (li < 0 || li >= static_cast<int>(set.levels.size()) || set.shapes[i].cells.empty())
                return fail(std::string(name) + " shape index is invalid");
            for (const auto& cell : set.shapes[i].cells)
                if (!finite(cell.dx) || !finite(cell.dy))
                    return fail(std::string(name) + " shape cell is invalid");
        }
        if (!set.iconLevels.empty() && set.iconLevels.size() != set.levels.size())
            return fail(std::string(name) + " iconLevels size mismatch");
        for (int level : set.iconLevels)
            if (level < 1 || level > 10) return fail(std::string(name) + " icon level invalid");
        return true;
    };
    if (!validateSet(city.square, "city.square") || !validateSet(city.hex, "city.hex")
        || !validateSet(city.tri, "city.tri"))
        return false;
    for (int i = static_cast<int>(TilingType::Arch33336); i < kTilingTypeCount; ++i)
        if (!validateSet(city.sets[static_cast<size_t>(i)], tilingName(static_cast<TilingType>(i))))
            return false;
    if (!nonNegative(city.levelIncomeExponent) || !positive(city.levelRankExponent))
        return fail("city exponent invalid");
    if (!positive(tech.thresholdBase) || !positive(tech.thresholdStep)
        || !nonNegative(tech.pointsPerCityLevel) || !nonNegative(tech.preferencePerLevel)
        || tech.playerCandidateCount <= 0)
        return fail("tech numeric range invalid");
    std::vector<std::string> techIds;
    for (const auto& def : tech.techs) {
        if (def.id.empty() || def.levels.empty()
            || std::find(techIds.begin(), techIds.end(), def.id) != techIds.end())
            return fail("tech ids must be non-empty and unique");
        techIds.push_back(def.id);
        for (const auto& level : def.levels)
            if (!finite(level.magnitude) || level.param < -1 || level.param >= kArmyTypeCount)
                return fail("tech level is invalid");
        for (int unit : def.preferenceUnits)
            if (unit < 0 || unit >= kArmyTypeCount) return fail("tech preference unit invalid");
    }

    for (const auto& [tiling, levels] : render.city.iconFitScale) {
        bool known = false;
        for (int i = 0; i < kTilingTypeCount; ++i)
            if (tiling == tilingName(static_cast<TilingType>(i))) known = true;
        if (!known) return fail("unknown city icon fit tiling");
        for (const auto& [level, value] : levels)
            if (level < 1 || level > 10 || !positive(value)) return fail("city icon fit is invalid");
    }
    for (const auto& [tiling, offsets] : render.city.iconFitOffsetY) {
        bool known = false;
        for (int i = 0; i < kTilingTypeCount; ++i)
            if (tiling == tilingName(static_cast<TilingType>(i))) known = true;
        if (!known) return fail("unknown city icon offset tiling");
        for (const auto& offset : offsets) {
            if (offset.iconLevel < 1 || offset.iconLevel > 10 || offset.variant < 0
                || !finite(offset.value))
                return fail("city icon offset is invalid");
            for (int base : offset.bases)
                if (base < 0 || base >= 32) return fail("city icon offset base is invalid");
        }
    }
    return true;
}

std::string Config::toJson() const {
    Json j;
    j["map"] = {{"blockSize", map.blockSize},
                {"panelWidth", map.panelWidth},
                {"capitalMinDistance", map.capitalMinDistance},
                {"cityMountainWeight", map.cityMountainWeight},
                {"tiling", map.tiling}};

    j["army"] = {{"baseSpeed", army.baseSpeed},
                  {"baseSize", army.baseSize},
                  {"bounceJitterRangeRad", army.bounceJitterRangeRad}};

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

    // P1.2：所有密铺形状统一以 cells=[dx,dy] 世界偏移序列化（square 不再输出旧 w/h）。
    const auto shapeArrayJ = [](const Config::City::TilingSet& set) {
        Json shapes = Json::array();
        for (int s = 0; s < static_cast<int>(set.shapes.size()); ++s) {
            Json cells = Json::array();
            for (const auto& c : set.shapes[static_cast<size_t>(s)].cells)
                cells.push_back({c.dx, c.dy});
            const int li = (s < static_cast<int>(set.shapeLevelIndex.size()))
                               ? set.shapeLevelIndex[static_cast<size_t>(s)]
                               : 0;
            Json shapeJ = {{"level", set.levels[static_cast<size_t>(li)]},
                           {"cells", cells}};
            if (set.shapes[static_cast<size_t>(s)].anchorBaseMask != 0)
                shapeJ["anchorBases"] = anchorMaskToBases(
                    set.shapes[static_cast<size_t>(s)].anchorBaseMask);
            shapes.push_back(std::move(shapeJ));
        }
        return shapes;
    };
    const auto tilingSetJ = [&shapeArrayJ](const Config::City::TilingSet& set) {
        Json result = Json{{"levels", set.levels}, {"shapes", shapeArrayJ(set)}};
        // 2026-08-17：贴图等级显式映射（缺省推导时不输出；与 levels 等长才输出）。
        if (!set.iconLevels.empty() && set.iconLevels.size() == set.levels.size())
            result["iconLevels"] = set.iconLevels;
        return result;
    };
    Json shapesJ = shapeArrayJ(city.square);
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

    // P13 城市渲染常数：不再输出 iconScale（P1.2 已删除；square 也走 iconFitScale）。
    {
        Json renderJ;
        renderJ["windowWidth"] = render.windowWidth;
        renderJ["windowHeight"] = render.windowHeight;
        renderJ["spriteSize"] = render.spriteSize;
        renderJ["armyDrawSize"] = render.armyDrawSize;
        // 双色系统（⑫）：地块格 主:副:白 混合比例。
        renderJ["tile"] = {{"primary", render.tileMix.primary},
                            {"secondary", render.tileMix.secondary},
                            {"white", render.tileMix.white},
                            {"variation", render.tileMix.variation}};

        Json cityJ;
        cityJ["lineMinCellPx"] = render.city.lineMinCellPx;
        cityJ["lineThickness"] = render.city.lineThickness;
        cityJ["lineDarken"] = render.city.lineDarken;
        cityJ["iconDarken"] = render.city.iconDarken;
        // 双色系统（⑫）：城市图标 主:副:黑 混合比例。
        cityJ["mix"] = {{"primary", render.city.mix.primary},
                        {"secondary", render.city.mix.secondary},
                        {"black", render.city.mix.black}};
        // iconFitScale / iconFitOffsetY 已外置（2026-08-26）：不写回 config.json。
        renderJ["city"] = std::move(cityJ);

        Json capitalJ;
        capitalJ["minIconSizePx"] = render.capital.minIconSizePx;
        capitalJ["lineThickness"] = render.capital.lineThickness;
        capitalJ["designatedAlpha"] = render.capital.designatedAlpha;
        renderJ["capital"] = std::move(capitalJ);

        j["render"] = std::move(renderJ);
    }
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

std::array<int, 3> Config::factionTileColor(int id, double t) const {
    // 地块格填色 = 主色:副色:白 加权平均（render.tileMix；⑫）。id 越界 → 中性灰占位。
    // 双色密铺分档（2026-08）：t∈[0,1] 渐变位置，主/副权重随 t 以 variation 为半宽对称摆动；
    // t=0.5 = 无渐变中点。权重非负夹取（variation 过大不越界）。
    std::array<int, 3> pri, sec;
    if (id < 0 || id >= static_cast<int>(factions.size())) {
        pri = {96, 96, 96};
        sec = {191, 191, 191};
    } else {
        const auto& f = factions[static_cast<size_t>(id)];
        pri = f.color;
        sec = f.secondary;
    }
    const double P = render.tileMix.primary;
    const double S = render.tileMix.secondary;
    const double W = render.tileMix.white;
    const double x = render.tileMix.variation;
    const double tt = std::clamp(t, 0.0, 1.0);
    double wp = P - x + 2.0 * x * tt;
    double ws = S + x - 2.0 * x * tt;
    if (wp < 0.0) wp = 0.0;
    if (ws < 0.0) ws = 0.0;
    return weightedMix3(pri, sec, {255, 255, 255}, wp, ws, W);
}

std::array<int, 3> Config::factionTileColor(int id) const {
    // 中点（t=0.5）＝无渐变的地块格填色（保持旧调用语义）。
    return factionTileColor(id, 0.5);
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
