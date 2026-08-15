// Snapshot.cpp — Simulation 全状态 JSON 序列化/反序列化（Phase 6）。
// 序列化内容：config（完整，与 loadFromJson 对称）、map 网格/首都/城市注册表、factions 运行时状态、
// 回合顺序、待产兵队列、entt registry（兵 + 特效全部组件）、rng mt 状态、ttime、goSea 概率。
// 还原时用全新 registry + create(hint)：entt 在索引空闲时精确复刻实体 id（含回收的版本位）。
#include "replay/Snapshot.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

#include "core/Simulation.h"
#include "sim/components.h"
#include "world/Faction.h"
#include "world/Map.h"

namespace lw {

namespace {
using Json = nlohmann::json;

constexpr int kSnapshotVersion = 6;  // v6：P12 异种地图（cells 按密铺格序 + 城市 baseIndex + config tiling）；v5 作废（P15 首都）

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

// 实体 id 合法性：index 必须在 [0, nullIndex) 内（entt::null 的 index 即实体 index 上限）。
bool validEntityId(std::uint32_t id) {
    const auto nullIndex =
        entt::to_entity(entt::entity{std::numeric_limits<std::uint32_t>::max()});
    return entt::to_entity(entt::entity{id}) < nullIndex;
}

}  // namespace

std::string Snapshot::serialize(const Simulation& sim) {
    Json root;
    root["version"] = kSnapshotVersion;
    root["tickCount"] = sim.tickCount();
    root["goSeaProbability"] = sim.goSeaProbability();
    root["nextEntityId"] = sim.nextEntityId_;  // 实体单调 id 分配器（读档后新实体 id 确定）

    // P11：统计（perFactionUnit + cutoffTick；汇总为派生，读档 recompute 重建）。
    root["stats"]["perFactionUnit"] = Json::array();
    for (int fid = 0; fid < kFactionTotal; ++fid) {
        Json fj = Json::array();
        for (int ut = 0; ut < kArmyTypeCount; ++ut) {
            const auto& s =
                sim.stats().perFactionUnit[static_cast<size_t>(fid)][static_cast<size_t>(ut)];
            fj.push_back({s.kills, s.lands, s.produced});
        }
        root["stats"]["perFactionUnit"].push_back(std::move(fj));
    }
    root["stats"]["cutoffTick"] = sim.stats().cutoffTick;

    root["config"] = Json::parse(sim.config().toJson());

    root["rng"]["seed"] = sim.rng().seedValue();
    root["rng"]["state"] = sim.rng().state();
    // P6：地图种子值（读档后主面板显示用；mapRng_ 状态不序列化——init 后不再消费）。
    root["mapSeed"] = sim.mapSeed();

    // ---- map：网格 + 首都 + 城市注册表（P13：cityId 取代 city bool，注册表单列）----
    // P12：cells 按**格下标序**序列化（方 = y*width+x 同旧序；六/三 = 密铺格序）；密铺类型
    // 由 config.map.tiling（快照自带 config）决定；城市增 baseIndex（形状锚点，权威）。
    const auto& map = sim.map();
    root["map"]["width"] = map.width();
    root["map"]["height"] = map.height();
    root["map"]["capitalsX"] = Json::array();
    root["map"]["capitalsY"] = Json::array();
    for (int i = 0; i < map.capitalCount(); ++i) {
        root["map"]["capitalsX"].push_back(map.capitalX(i));
        root["map"]["capitalsY"].push_back(map.capitalY(i));
    }
    root["map"]["cities"] = Json::array();
    for (const auto& c : map.cities())
        root["map"]["cities"].push_back(
            {c.id, c.ownerId, c.level, c.baseX, c.baseY, c.w, c.h, c.lastProduceArmyN,
             c.lastCapturedTick, c.baseIndex});
    root["map"]["cells"] = Json::array();
    for (int idx = 0; idx < map.cellCount(); ++idx) {
        const MapCell& c = map.atIndex(idx);
        root["map"]["cells"].push_back({c.belongi, c.land, c.mountain, c.cityId});
    }

    // ---- factions ----
    root["factions"] = Json::array();
    for (const auto& f : sim.factions()) {
        Json fj;
        fj["id"] = f.id;
        fj["alive"] = f.alive;
        fj["aiId"] = f.aiId;  // 产兵 AI id（P1/P2；读档后玩家势力不退回默认 AI）
        fj["color"] = {f.color[0], f.color[1], f.color[2]};
        fj["cityCount"] = f.cityCount;
        fj["landCount"] = f.landCount;
        fj["numArmyProduced"] = f.numArmyProduced;
        fj["economy"] = f.economy;  // 留存值（库存）
        fj["freeArmyChance"] = f.freeArmyChance;
        // P15：首都迁都状态（CapitalState；v5）。capitalCityId 读档后沿用（初始首都由 init 设）。
        fj["capital"] = {{"capitalCityId", f.capitalState.capitalCityId},
                         {"designatedCityId", f.capitalState.designatedCityId},
                         {"lostTick", f.capitalState.lostTick},
                         {"immediateRelocate", f.capitalState.immediateRelocate}};
        fj["armyCost"] = Json::array();
        fj["producedCost"] = Json::array();  // 公平调度堆的累计花费
        fj["unitPreference"] = Json::array();  // P11 改版：兵种偏好系数（默认 AI 排序键）
        for (int i = 0; i < kArmyTypeCount; ++i) {
            fj["armyCost"].push_back(f.armyCost[static_cast<size_t>(i)]);
            fj["producedCost"].push_back(f.producedCost[static_cast<size_t>(i)]);
            fj["unitPreference"].push_back(f.unitPreference[static_cast<size_t>(i)]);
        }
        fj["cityIds"] = Json::array();  // P13：城市 id 列表（Map 注册表下标）
        for (int cid : f.cityIds) fj["cityIds"].push_back(cid);
        // P8：科技状态（buffs 不入快照，读档由 techBuffs(cfg, levels) 重建）。
        fj["tech"] = {{"points", f.tech.points},
                      {"threshold", f.tech.threshold},
                      {"levels", f.tech.levels},
                      {"pending", f.tech.researchPending},
                      {"candidates", f.tech.candidates}};
        root["factions"].push_back(std::move(fj));
    }

    // ---- 回合顺序 / 待产兵 ----
    root["turnOrder"] = sim.turnOrder();
    root["pendingSpawns"] = Json::array();
    for (const auto& ps : sim.pendingSpawns())
        root["pendingSpawns"].push_back({ps.type, ps.factionId, ps.x, ps.y});

    // ---- registry：兵 + 子弹 + 特效 ----
    // P9 修正：实体**按 Position 存储序**（reach）统一序列化（kind 字段区分兵/子弹/特效）。
    // 系统每 tick 的 RNG 抽取顺序由视图迭代序决定，移动/空间哈希以 Position 为驱动存储；
    // 实体销毁的 swap-and-pop 会在各存储留下不同的 packed 序（异构存储），故只有按
    // Position 序原样重建，移动与碰撞候选顺序才能与直跑逐位一致。此前按兵专用存储
    // UnitType 序列化，在子弹混入该存储后（以及旧版兵+特效并存时）顺序已不可复现。
    // 已知限制（P4 起）：兵/子弹/特效各存储 packed 序在实体回收后互不相同，任一单一重建
    // 序都无法同时复现全部 → 复杂轨迹（有死亡+异类实体并存）的**续跑**可能逐 tick 漂移；
    // 当前状态与安静轨迹的续跑逐位一致（见 test_snapshot 注）。
    const auto& reg = sim.registry();
    root["registry"]["entities"] = Json::array();
    if (const auto* ps = reg.storage<comp::Position>(); ps) {
        for (auto elem : ps->reach()) {
            const auto e = std::get<0>(elem);
            Json a;
            a["id"] = entt::to_integral(e);
            const auto& p = reg.get<comp::Position>(e);
            a["pos"] = {p.x, p.y};
            if (reg.all_of<comp::Projectile>(e)) {
                // 子弹
                a["kind"] = 1;
                a["vel"] = reg.get<comp::Velocity>(e).angle;
                a["speed"] = reg.get<comp::Speed>(e).value;
                a["fid"] = reg.get<comp::FactionId>(e).value;
                a["type"] = static_cast<int>(reg.get<comp::UnitType>(e).type);
                a["radius"] = reg.get<comp::Collider>(e).radius;
                a["lifespan"] = reg.get<comp::Projectile>(e).lifespanTicks;
                a["inMountain"] = reg.get<comp::Projectile>(e).inMountain;
            } else if (reg.all_of<comp::UnitType>(e)) {
                // 兵
                a["kind"] = 0;
                a["vel"] = reg.get<comp::Velocity>(e).angle;
                a["speed"] = reg.get<comp::Speed>(e).value;
                a["onLand"] = reg.get<comp::OnLand>(e).value;
                a["fid"] = reg.get<comp::FactionId>(e).value;
                a["type"] = static_cast<int>(reg.get<comp::UnitType>(e).type);
                a["radius"] = reg.get<comp::Collider>(e).radius;
                a["lastLand"] = reg.get<comp::LandHistory>(e).lastLandTime;
                a["inMountain"] = reg.get<comp::MountainState>(e).inMountain;
                a["dead"] = reg.all_of<comp::Dead>(e);
                // P9 行为计数（快照后周期动作继续按原节奏触发）。
                const auto* bh = reg.try_get<comp::Behavior>(e);
                a["periodCounter"] = bh ? bh->counter : 0;
            } else {
                // 特效
                a["kind"] = 2;
                a["fid"] = reg.get<comp::FactionId>(e).value;
                a["type"] = static_cast<int>(reg.get<comp::EffectTypeId>(e).type);
                a["created"] = reg.get<comp::EffectTimer>(e).createdTick;
                const auto& ep = reg.get<comp::EffectParams>(e);
                a["p0"] = ep.p0;
                a["p1"] = ep.p1;
                a["p2"] = ep.p2;
                const auto& creator = reg.get<comp::Creator>(e);
                // P11：creator.unitType（击杀/占领 credit 链）入快照。
                a["creator"] = {creator.x, creator.y, creator.factionId, creator.unitType};
            }
            root["registry"]["entities"].push_back(std::move(a));
        }
    }

    return root.dump(2);
}

bool Snapshot::deserialize(Simulation& sim, const std::string& json, std::string* err) {
    Json root;
    try {
        root = Json::parse(json);
    } catch (const std::exception&) {
        setErr(err, "snapshot: invalid JSON");
        return false;
    }
    if (!root.is_object() || root.value("version", 0) != kSnapshotVersion) {
        setErr(err, "snapshot: unsupported version");
        return false;
    }

    // ---- config（快照自带完整配置，与 loadFromJson 对称，往返无损）----
    sim.config_ = Config::loadFromJson(root.at("config").dump());

    // ---- rng：恢复 mt19937 完整内部状态（seed 仅供展示/复现，序列以 state 为准）----
    if (!root.contains("rng") || !root["rng"].is_object()) {
        setErr(err, "snapshot: missing rng");
        return false;
    }
    const auto& rj = root["rng"];
    if (!rj.contains("state") || !rj["state"].is_string()) {
        setErr(err, "snapshot: bad rng state");
        return false;
    }
    sim.rng_ = std::make_unique<Rng>(rj.value("seed", 0u));
    sim.rng_->setState(rj["state"].get<std::string>());
    // P6：地图种子值恢复（主面板显示；mapRng_ 仅在 init 时消费，读档后无需重建状态）。
    sim.mapSeed_ = root.value("mapSeed", 0u);

    // ---- map：网格 + 首都 + 城市注册表（P13/P12）----
    const auto& mj = root.at("map");
    Map& m = sim.map_;
    m.width_ = mj.at("width").get<int>();
    m.height_ = mj.at("height").get<int>();
    // P12：密铺几何（来自快照 config；默认 square → 旧档兼容）。
    m.geom_ = TilingGeom{tilingFromName(sim.config_.map.tiling), m.width_, m.height_};
    m.capitalX_ = mj.at("capitalsX").get<std::vector<int>>();
    m.capitalY_ = mj.at("capitalsY").get<std::vector<int>>();
    m.cityConfig_ = sim.config_.city;  // 城市形状表与快照 config 保持一致
    m.cities_.clear();
    if (mj.contains("cities") && mj["cities"].is_array()) {
        for (const auto& cj : mj["cities"]) {
            City c;
            c.id = cj[0].get<int>();
            c.ownerId = cj[1].get<int>();
            c.level = cj[2].get<double>();
            c.baseX = cj[3].get<int>();
            c.baseY = cj[4].get<int>();
            c.w = cj[5].get<int>();
            c.h = cj[6].get<int>();
            c.lastProduceArmyN = cj[7].get<int>();
            c.lastCapturedTick = cj[8].get<std::uint64_t>();
            // P12：baseIndex（权威锚点；旧档无该键 → 由 baseX/baseY 按密铺重推）。
            if (cj.size() > 9)
                c.baseIndex = cj[9].get<int>();
            else
                c.baseIndex = m.cellIndexAt(c.baseX, c.baseY);
            m.cities_.push_back(c);
        }
    }
    m.recomputeCityGeometry();  // P12：baseIndex/几何中心/AABB（方 = 旧式同值）
    // P12：cells 按密铺格序（方 = width*height 同旧；六/三 = 密铺 cellCount）。
    const size_t cellCount = static_cast<size_t>(m.geom_.cellCount());
    const auto& cells = mj.at("cells");
    if (cells.size() != cellCount) {
        setErr(err, "snapshot: map cells size mismatch");
        return false;
    }
    m.cells_.assign(cellCount, MapCell{});
    for (size_t i = 0; i < cellCount; ++i) {
        const auto& c = cells[i];
        MapCell& out = m.cells_[i];
        if (m.geom_.type == TilingType::Tri) {  // 格坐标 (i 对, r 行)
            out.x = static_cast<int>((i >> 1) % static_cast<size_t>(m.geom_.cols));
            out.y = static_cast<int>((i >> 1) / static_cast<size_t>(m.geom_.cols));
        } else if (m.geom_.type == TilingType::Hex) {
            out.x = static_cast<int>(i % static_cast<size_t>(m.geom_.cols));
            out.y = static_cast<int>(i / static_cast<size_t>(m.geom_.cols));
        } else if (static_cast<int>(m.geom_.type) > static_cast<int>(TilingType::Tri)) {
            int rr, cc, bb;
            m.geom_.indexToRowCol(static_cast<int>(i), rr, cc, bb);
            out.x = cc;
            out.y = rr;
        } else {
            out.x = static_cast<int>(i % static_cast<size_t>(m.width_));
            out.y = static_cast<int>(i / static_cast<size_t>(m.width_));
        }
        out.belongi = c[0].get<int>();
        out.land = c[1].get<bool>();
        out.mountain = c[2].get<bool>();
        out.cityId = c[3].get<int>();
    }

    // ---- factions ----
    sim.factions_.clear();
    for (const auto& fj : root.at("factions")) {
        Faction f;
        f.id = fj.value("id", 0);
        f.alive = fj.value("alive", false);
        f.aiId = fj.value("aiId", 0);  // 向前兼容：旧档无该键 → 0（默认 AI）
        const auto& color = fj.at("color");
        for (int i = 0; i < 3; ++i)
            f.color[static_cast<size_t>(i)] = color[i].get<int>();
        f.cityCount = fj.value("cityCount", 0);
        f.landCount = fj.value("landCount", 0);
        f.numArmyProduced = fj.value("numArmyProduced", 0);
        f.economy = fj.value("economy", 0.0);  // 留存值（库存）
        f.freeArmyChance = fj.value("freeArmyChance", 0.0);
        // P15：首都状态（value(key, default) 向前兼容；旧档无 capital 键 → 无正式首都）。
        if (fj.contains("capital")) {
            const auto& cj = fj.at("capital");
            f.capitalState.capitalCityId = cj.value("capitalCityId", -1);
            f.capitalState.designatedCityId = cj.value("designatedCityId", -1);
            f.capitalState.lostTick = cj.value("lostTick", 0ULL);
            f.capitalState.immediateRelocate = cj.value("immediateRelocate", false);
        }
        const auto& ac = fj.at("armyCost");
        const auto& pc = fj.at("producedCost");  // 公平调度堆的累计花费
        for (int i = 0; i < kArmyTypeCount; ++i) {
            f.armyCost[static_cast<size_t>(i)] = ac[i].get<double>();
            f.producedCost[static_cast<size_t>(i)] = pc[i].get<double>();
        }
        // P11 改版：兵种偏好系数（旧档无该键 → 默认全 1.0）。
        f.unitPreference.fill(1.0);
        if (fj.contains("unitPreference") && fj["unitPreference"].is_array()) {
            const auto& up = fj["unitPreference"];
            for (int i = 0; i < kArmyTypeCount && i < static_cast<int>(up.size()); ++i)
                f.unitPreference[static_cast<size_t>(i)] = up[i].get<double>();
        }
        f.cityIds.clear();
        if (fj.contains("cityIds")) {
            for (const auto& cid : fj.at("cityIds")) f.cityIds.push_back(cid.get<int>());
        }
        f.cityCount = static_cast<int>(f.cityIds.size());  // 与 cityIds 一致（防分叉）
        // P8：科技状态（向前兼容：旧档无 tech → 默认无科技；levels 由 config.techs 驱动）。
        f.tech = TechState{};
        if (fj.contains("tech")) {
            const auto& tj = fj.at("tech");
            f.tech.points = tj.value("points", 0.0);
            f.tech.threshold = tj.value("threshold", 0.0);
            if (tj.contains("levels") && tj["levels"].is_array())
                f.tech.levels = tj["levels"].get<std::vector<int>>();
            f.tech.researchPending = tj.value("pending", false);
            if (tj.contains("candidates") && tj["candidates"].is_array())
                f.tech.candidates = tj["candidates"].get<std::vector<int>>();
        }
        sim.factions_.push_back(std::move(f));
    }
    // P7/P8：buffs 不入快照（初始 buff 恒可自定义重建；科技 buff 从 tech.levels 重建）→
    // 读档后从势力定义 + 科技等级重建 buffs + mods，与直跑一致（快照字节不变）。
    for (auto& f : sim.factions_) {
        const size_t fi = static_cast<size_t>(f.id);
        if (fi < sim.config_.factions.size())
            f.rebuildBuffsFromDef(sim.config_.factions[fi], sim.config_);
    }

    // ---- 回合顺序 / 待产兵 ----
    sim.turnOrder_ = root.at("turnOrder").get<std::vector<int>>();
    sim.pendingSpawns_.clear();
    if (root.contains("pendingSpawns")) {
        for (const auto& pj : root["pendingSpawns"])
            sim.pendingSpawns_.push_back(
                {pj[0].get<int>(), pj[1].get<int>(), pj[2].get<double>(), pj[3].get<double>()});
    }

    // ---- registry：全新 registry 使 create(hint) 精确还原实体 id ----
    // 按序列化数组原序（= 原始 Position 存储序）重建，保持移动/空间哈希候选顺序与直跑一致。
    sim.registry_ = entt::registry{};
    auto& reg = sim.registry_;
    const auto& regJ = root.at("registry");
    if (!regJ.contains("entities") || !regJ["entities"].is_array()) {
        setErr(err, "snapshot: missing registry entities");
        return false;
    }
    for (const auto& ej : regJ.at("entities")) {
        const std::uint32_t id = ej.at("id").get<std::uint32_t>();
        if (!validEntityId(id)) {
            setErr(err, "snapshot: entity id out of range");
            return false;
        }
        auto e = reg.create(entt::entity{id});
        const int kind = ej.value("kind", -1);
        const auto& pos = ej.at("pos");
        reg.emplace<comp::Position>(e, pos[0].get<double>(), pos[1].get<double>());
        if (kind == 1) {  // 子弹
            reg.emplace<comp::Velocity>(e, ej.at("vel").get<double>());
            reg.emplace<comp::Speed>(e, ej.at("speed").get<double>());
            reg.emplace<comp::FactionId>(e, ej.at("fid").get<int>());
            reg.emplace<comp::UnitType>(e, static_cast<ArmyType>(ej.at("type").get<int>()));
            reg.emplace<comp::Collider>(e, ej.at("radius").get<double>());
            reg.emplace<comp::Projectile>(e, comp::Projectile{ej.at("lifespan").get<int>(),
                                                              ej.value("inMountain", false)});
        } else if (kind == 0) {  // 兵
            reg.emplace<comp::Velocity>(e, ej.at("vel").get<double>());
            reg.emplace<comp::Speed>(e, ej.at("speed").get<double>());
            reg.emplace<comp::OnLand>(e, ej.at("onLand").get<bool>());
            reg.emplace<comp::FactionId>(e, ej.at("fid").get<int>());
            reg.emplace<comp::UnitType>(e, static_cast<ArmyType>(ej.at("type").get<int>()));
            reg.emplace<comp::Collider>(e, ej.at("radius").get<double>());
            reg.emplace<comp::LandHistory>(e, ej.at("lastLand").get<int>());
            reg.emplace<comp::MountainState>(e, ej.value("inMountain", false));
            // P9 行为：静态字段从兵种定义重填（与 spawnArmy 一致），仅 counter 从快照恢复。
            const int armyType = ej.at("type").get<int>();
            const auto& udef = sim.config_.units[static_cast<size_t>(armyType)];
            reg.emplace<comp::Behavior>(e, comp::Behavior{udef.deathEffect, udef.periodic,
                                                          udef.periodTicks,
                                                          ej.value("periodCounter", 0)});
            if (ej.value("dead", false)) reg.emplace<comp::Dead>(e);
        } else {  // 特效
            reg.emplace<comp::FactionId>(e, ej.at("fid").get<int>());
            reg.emplace<comp::EffectTypeId>(e, static_cast<EffectType>(ej.at("type").get<int>()));
            reg.emplace<comp::EffectTimer>(e, ej.at("created").get<int>());
            reg.emplace<comp::EffectParams>(e, ej.at("p0").get<double>(), ej.at("p1").get<double>(),
                                            ej.at("p2").get<double>());
            const auto& cr = ej.at("creator");
            // P11：unitType 向后兼容（旧档 3 元素 → 默认 0）。
            const int crUnitType = cr.size() > 3 ? cr[3].get<int>() : 0;
            reg.emplace<comp::Creator>(e,
                                       comp::Creator{cr[0].get<double>(), cr[1].get<double>(),
                                                     cr[2].get<int>(), crUnitType});
        }
    }

    sim.goSeaProbability_ = root.at("goSeaProbability").get<double>();
    sim.tickCount_ = root.at("tickCount").get<std::uint64_t>();

    // P11：统计（perFactionUnit + cutoffTick；汇总 recompute 重建；旧档无 stats → 默认全 0）。
    if (root.contains("stats")) {
        const auto& st = root["stats"];
        const auto& pu = st.contains("perFactionUnit") ? st["perFactionUnit"] : Json();
        if (pu.is_array()) {
            for (int fid = 0; fid < kFactionTotal; ++fid) {
                for (int ut = 0; ut < kArmyTypeCount; ++ut) {
                    if (fid >= static_cast<int>(pu.size())) break;
                    const auto& v = pu[static_cast<size_t>(fid)];
                    if (ut >= static_cast<int>(v.size())) break;
                    auto& s = sim.stats_.perFactionUnit[static_cast<size_t>(fid)]
                                                        [static_cast<size_t>(ut)];
                    s.kills = v[static_cast<size_t>(ut)][0].get<int>();
                    s.lands = v[static_cast<size_t>(ut)][1].get<int>();
                    s.produced = v[static_cast<size_t>(ut)][2].get<int>();
                }
            }
        }
        sim.stats_.cutoffTick = st.value("cutoffTick", 0ULL);
        sim.stats_.recompute();  // 汇总（factionKills/unitKills…）为派生，读档重建
    }
    sim.nextEntityId_ = root.value("nextEntityId", 1u);  // 旧存档无此键 → 1（向前兼容）
    return true;
}

bool Snapshot::saveToFile(const Simulation& sim, const std::string& path, std::string* err) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        setErr(err, "snapshot: cannot open '" + path + "' for writing");
        return false;
    }
    ofs << serialize(sim);
    if (!ofs.good()) {
        setErr(err, "snapshot: write failed for '" + path + "'");
        return false;
    }
    return true;
}

bool Snapshot::loadFromFile(Simulation& sim, const std::string& path, std::string* err) {
    std::ifstream ifs(path);
    // MinGW：文件不存在须用 is_open() 判断（Phase 1 已踩坑）。
    if (!ifs.is_open()) {
        setErr(err, "snapshot: file '" + path + "' not found");
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return deserialize(sim, oss.str(), err);
}

}  // namespace lw
