// Options.cpp — 菜单选项 json 序列化（开发计划 P1）。
#include "core/Options.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace lw {

namespace {

using Json = nlohmann::json;

bool getBool(const Json& j, const char* key, bool def) {
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
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
float getFloat(const Json& j, const char* key, float def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<float>();
    return def;
}
double getDouble(const Json& j, const char* key, double def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
    return def;
}

}  // namespace

bool Options::validate(std::string* err) const {
    int players = 0;
    for (const auto& slot : factions) {
        if (slot.aiId == 1) ++players;
    }
    if (players > 1) {
        if (err) *err = "最多只能有一个势力由玩家操控 (aiId=1)";
        return false;
    }
    return true;
}

Options Options::loadFromJson(const std::string& jsonText) {
    Options o;

    Json root;
    try {
        root = Json::parse(jsonText);
    } catch (const std::exception& e) {
        spdlog::warn("options.json parse failed ({}), using defaults", e.what());
        return o;
    }

    if (root.contains("factions") && root["factions"].is_array()) {
        int i = 0;
        for (const auto& slotJson : root["factions"]) {
            if (i >= kPlayerFactionCount) break;
            if (!slotJson.is_object()) continue;
            o.factions[static_cast<size_t>(i)].enabled = getBool(slotJson, "enabled", true);
            o.factions[static_cast<size_t>(i)].aiId = getInt(slotJson, "aiId", 0);
            ++i;
        }
    }

    if (root.contains("map") && root["map"].is_object()) {
        const auto& mapJson = root["map"];
        const std::string kind = getStr(mapJson, "kind", "file");
        o.map.kind = (kind == "random") ? MapSelection::Kind::Random : MapSelection::Kind::File;
        o.map.file = getStr(mapJson, "file", o.map.file);
        o.map.randomSeed = static_cast<std::uint32_t>(getInt(mapJson, "randomSeed", 0));
        // P6 随机图参数：缺键回退默认（旧 options.json 无这些键也能读）。
        o.map.width = getInt(mapJson, "width", o.map.width);
        o.map.height = getInt(mapJson, "height", o.map.height);
        // 用 double 读（float 读会损失精度 → EXPECT_DOUBLE_EQ 往返失败）。
        o.map.seaRatio = getDouble(mapJson, "seaRatio", o.map.seaRatio);
        o.map.mountainDensity = getDouble(mapJson, "mountainDensity", o.map.mountainDensity);
        o.map.cityDensity = getDouble(mapJson, "cityDensity", o.map.cityDensity);
        o.map.forceCoast = getBool(mapJson, "forceCoast", false);
        o.map.wrap = getBool(mapJson, "wrap", false);
    }

    // 面板布局（P3；缺键回退空 → 新装/旧 options.json 无面板状态，用默认布局）。
    if (root.contains("panels") && root["panels"].is_array()) {
        for (const auto& pj : root["panels"]) {
            if (!pj.is_object()) continue;
            PanelLayout p;
            p.id = getStr(pj, "id", "");
            if (p.id.empty()) continue;
            p.visible = getBool(pj, "visible", false);
            p.x = getFloat(pj, "x", 0.0f);
            p.y = getFloat(pj, "y", 0.0f);
            p.w = getFloat(pj, "w", 0.0f);
            p.h = getFloat(pj, "h", 0.0f);
            o.panels.push_back(p);
        }
    }

    return o;
}

Options Options::loadFromFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        spdlog::info("options file '{}' not found, using defaults", path);
        return Options{};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return loadFromJson(oss.str());
}

std::string Options::toJson() const {
    Json j;
    j["factions"] = Json::array();
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        const auto& slot = factions[static_cast<size_t>(i)];
        j["factions"].push_back({{"enabled", slot.enabled}, {"aiId", slot.aiId}});
    }
    // P6 随机图参数：与 loadFromJson 键严格对称（快照/options 往返依赖）。
    j["map"] = {{"kind", map.kind == MapSelection::Kind::Random ? "random" : "file"},
                {"file", map.file},
                {"randomSeed", map.randomSeed},
                {"width", map.width},
                {"height", map.height},
                {"seaRatio", map.seaRatio},
                {"mountainDensity", map.mountainDensity},
                {"cityDensity", map.cityDensity},
                {"forceCoast", map.forceCoast},
                {"wrap", map.wrap}};
    // 面板布局（P3）：与 loadFromJson 键严格对称。
    j["panels"] = Json::array();
    for (const auto& p : panels) {
        j["panels"].push_back({{"id", p.id},
                               {"visible", p.visible},
                               {"x", p.x},
                               {"y", p.y},
                               {"w", p.w},
                               {"h", p.h}});
    }
    return j.dump(2);
}

void Options::saveToFile(const std::string& path) const {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        spdlog::warn("options save to '{}' failed (write error?)", path);
        return;
    }
    ofs << toJson();
    spdlog::info("options saved to '{}'", path);
}

}  // namespace lw
