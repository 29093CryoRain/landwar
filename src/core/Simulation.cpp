#include "core/Simulation.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

#include <spdlog/spdlog.h>

#include "sim/systems/CapitalSystem.h"
#include "sim/systems/DeathSystem.h"
#include "sim/systems/EconomySystem.h"
#include "sim/systems/EffectSystem.h"
#include "sim/systems/MovementSystem.h"
#include "sim/systems/ProductionSystem.h"
#include "sim/systems/ProjectileSystem.h"
#include "sim/systems/SpawnSystem.h"
#include "sim/systems/TechSystem.h"
#include "sim/systems/UnitActionSystem.h"

namespace lw {

namespace {
// 内置默认势力表（Config.cpp 内置；当 config_ 未走 loadFromJson 时补齐，保证 0..8 齐全）。
std::vector<Config::Faction> defaultFactions() {
    Config def = Config::loadFromJson("{}");
    return def.factions;
}
}  // namespace

Simulation::Simulation() : rng_(std::make_unique<Rng>()) {}

Simulation::Simulation(const Config& config, std::uint32_t seed)
    : config_(config), rng_(std::make_unique<Rng>(seed)), mapSeed_(seed) {}

Simulation::Simulation(const Config& config, std::uint32_t seed, std::uint32_t mapSeed)
    : config_(config), rng_(std::make_unique<Rng>(seed)), mapSeed_(mapSeed) {}

bool Simulation::init(const Options& options) {
    options_ = options;
    return init();
}

bool Simulation::init() {
    if (config_.factions.size() != static_cast<size_t>(kFactionTotal)) {
        config_.factions = defaultFactions();
    }
    map_.configure(config_.map);
    map_.setTerrain(config_.terrain);  // 山地参数（P5）：亮度带 + 邻海修正
    map_.setCityConfig(config_.city);  // 城市等级形状表（P13，思路 9.1）
    // P6 RNG 分离：山/城骰子走 mapRng_（地图种子）；首都与后续走 rng_（主种子）。
    mapRng_ = std::make_unique<Rng>(mapSeed_);
    if (!map_.loadFromBmp(config_.map.file, *mapRng_)) return false;  // 每陆地格 2 次 chance
    if (!map_.placeCapitals(*rng_)) return false;                     // 每 attempt get(w-1)+get(h-1)
    map_.finalize();                                               // 清理海上城市 + 统计
    initFactions();  // 征服 8 个首都（势力8 无开局免费兵，见 §1.4）
    // 中立势力0 landCount 初始 = 无主领地数（2026-08 用户定夺）：随征服递减，全图被占完恰好归 0
    // （原版从 0 起一路减负）。仅 1..8 参与排行榜，不影响模拟逻辑。
    factions_[0].landCount = 0;
    for (int y = 0; y < map_.height(); ++y)
        for (int x = 0; x < map_.width(); ++x) {
            const MapCell& c = map_.at(x, y);
            if (c.land && c.belongi == kNeutralFaction) ++factions_[0].landCount;
        }
    processPendingSpawns();  // init 阶段无势力8 免费兵 → 本阶段恒为空
    return true;
}

void Simulation::initFactions() {
    factions_.clear();
    factions_.resize(static_cast<size_t>(kFactionTotal));
    for (int id = 0; id < kFactionTotal; ++id) {
        factions_[static_cast<size_t>(id)].initFromDef(config_.factions[static_cast<size_t>(id)],
                                                       config_);
    }
    // P1：Options 决定势力出场与产兵 AI（默认全启用 aiId=0 → 行为与旧版一致）。
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        Faction& f = factions_[static_cast<size_t>(i + 1)];
        f.alive = options_.factions[static_cast<size_t>(i)].enabled;
        f.aiId = options_.factions[static_cast<size_t>(i)].aiId;
    }

    // 回合顺序：初始为 0..7 排列（原版 random_shuffle；新版用注入 Rng 的 Fisher-Yates）。
    // 无论势力是否禁用都执行洗牌（消耗相同 RNG），保证确定性不受 options 影响。
    turnOrder_.resize(static_cast<size_t>(kPlayerFactionCount));
    std::iota(turnOrder_.begin(), turnOrder_.end(), 0);
    for (int i = kPlayerFactionCount - 1; i > 0; --i) {
        const int j = rng_->get(i);  // [0, i]
        std::swap(turnOrder_[static_cast<size_t>(i)], turnOrder_[static_cast<size_t>(j)]);
    }

    // 按打乱顺序征服首都：势力 i+1 征服第 turnOrder_[i] 个首都（原版 Init_faction 对应行）。
    // freeArmyEnabled=false：init 阶段不触发势力8 免费产兵（用户定夺 2026-08，去掉开局免费兵）。
    // 禁用势力跳过征服 → 其首都保持中立（belongi=0，city=1），仍计入 totalCities。
    for (int i = 0; i < kPlayerFactionCount; ++i) {
        if (!options_.factions[static_cast<size_t>(i)].enabled) continue;
        const int capIdx = turnOrder_[static_cast<size_t>(i)];
        const int capX = map_.capitalX(capIdx);
        const int capY = map_.capitalY(capIdx);
        conquer(capX, capY, i + 1, /*freeArmyEnabled=*/false);
        // P15：初始首都 = 初始城（征服后该格所属城市 id）。首都语义本阶段起生效。
        const int fid = i + 1;
        Faction& f = factions_[static_cast<size_t>(fid)];
        const int capCityId = map_.at(capX, capY).cityId;
        f.capitalState.capitalCityId = capCityId;
        // 回归（用户反馈）：初始首都是**多格城**（锚点落在既有 2/4/6/9 级城）时，单格征服只改
        // 一格 belongi → 城市不整块转移（其余基建格仍中立）→ 势力 cityCount=0 → 立即灭亡。
        // 修复：把首都城其余基建格也逐格征服（末格触发整城转移，ownerId 归本势力、
        // lastCapturedTick=0）。init 阶段 freeArmyEnabled=false → 这些征服**不消耗 RNG**，
        // 且单格首都（常见）零额外调用 → 基线/确定性不变。
        if (capCityId >= 0 && map_.city(capCityId).ownerId != fid) {
            const City& city = map_.city(capCityId);
            for (int dy = 0; dy < city.h; ++dy)
                for (int dx = 0; dx < city.w; ++dx) {
                    const int cx = city.baseX + dx, cy = city.baseY + dy;
                    if (map_.at(cx, cy).belongi != fid)
                        conquer(cx, cy, fid, /*freeArmyEnabled=*/false);
                }
        }
    }
}

// P15：指定候补新都（玩家经 DebugPanel"指定首都"点击己方城市调用；AI 走 CapitalSystem）。
bool Simulation::setDesignatedCapital(int factionId, int cityId) {
    if (factionId <= 0 || factionId >= kFactionTotal) return false;
    Faction& f = factions_[static_cast<size_t>(factionId)];
    if (!f.alive) return false;
    if (f.capitalState.capitalCityId >= 0) return false;  // 有正式首都不可指定
    if (cityId < 0 || cityId >= map_.cityCount()) return false;
    if (map_.city(cityId).ownerId != factionId) return false;  // 城市须己方
    f.capitalState.designatedCityId = cityId;
    return true;
}

// P8：玩家科研三选一结算（Application 在科研弹窗点选后调用）。
// 应用（techIndex>=0）：升级该科技（等级+1）+ 阈值提升 + 清点。
// 跳过（techIndex==-1，2026-08 细节改进）：**下次升级所需科技点数不变**——只清空已积攒
// 的科技点与待选（科研机会消耗），阈值/等级都不变（不再有"跳过罚阈值"）。
bool Simulation::choosePlayerTech(int techIndex) {
    // 找玩家势力（aiId==1；最多一个）。
    int fid = -1;
    for (int id = 1; id <= kPlayerFactionCount; ++id)
        if (factions_[static_cast<size_t>(id)].aiId == 1) { fid = id; break; }
    if (fid < 0) return false;
    Faction& f = factions_[static_cast<size_t>(fid)];
    if (!f.tech.researchPending) return false;
    if (techIndex >= 0) {
        // 非法候选（不在 sampled candidates 内）→ 不应用（防御）。
        bool inCandidates = false;
        for (int c : f.tech.candidates)
            if (c == techIndex) { inCandidates = true; break; }
        if (!inCandidates) return false;
        TechSystem::applyTech(*this, fid, techIndex);
        // 结算：清空科技点 + 阈值提升 + 清除待选（升级后下一阈值更高）。
        f.tech.points = 0.0;
        f.tech.threshold += config_.tech.thresholdStep;
    } else {
        // 跳过：清空已积攒科技点 + 待选（避免下次 tick 立即重触发死循环）；
        // 阈值与等级**不变**（细节改进：跳过不罚阈值）。
        f.tech.points = 0.0;
    }
    f.tech.researchPending = false;
    f.tech.candidates.clear();
    return true;
}

void Simulation::conquer(int x, int y, int factionId, bool freeArmyEnabled) {
    ConquerContext ctx{map_, factions_, *rng_, pendingSpawns_, freeArmyEnabled, tickCount_};
    factions_[static_cast<size_t>(factionId)].conquer(ctx, x, y);
}

void Simulation::processPendingSpawns() {
    for (const auto& ps : pendingSpawns_) {
        SpawnSystem::spawnArmy(*this, ps.x, ps.y, ps.factionId, static_cast<ArmyType>(ps.type));
    }
    pendingSpawns_.clear();
}

void Simulation::tick() {
    // §2.10 顺序：军队（移动+战斗）→ 势力8 免费兵（原版征服时即产，故紧随军队阶段）
    // → P9 周期射击（UnitAction）→ P9 子弹求解（Projectile）→ 经济（回合顺序洗牌 + 累积）
    // → 产兵 → 特效（爆炸/地雷/激光）→ 死亡处理 → ttime++ → 灭亡/统一检测（P4）→ go_sea。
    MovementSystem::update(*this);
    processPendingSpawns();
    UnitActionSystem::update(*this);   // P9：周期发射（霰弹消耗 RNG；手枪 0）
    ProjectileSystem::update(*this);   // P9：子弹移动/命中/占领/超时
    EconomySystem::update(*this);
    ProductionSystem::update(*this);
    EffectSystem::update(*this);
    DeathSystem::flush(*this);
    CapitalSystem::update(*this);  // P15：迁都状态机（纯逻辑、无 RNG；先于灭亡检测，顺序固定）
    TechSystem::update(*this);     // P8：科技（点累积 + 阈值科研；AI 消耗 RNG，确定性）
    ++tickCount_;
    detectAnnihilationAndUnification();  // 纯逻辑，不消耗 Rng
    goSeaProbability_ += config_.sea.goSeaIncrease;
}

std::vector<DeathEvent> Simulation::takeDeaths() {
    std::vector<DeathEvent> out;
    std::swap(out, deaths_);
    return out;
}

std::vector<GameEvent> Simulation::takeEvents() {
    std::vector<GameEvent> out;
    std::swap(out, events_);
    return out;
}

int Simulation::countArmies(int factionId) const {
    // 兵带 Collider（特效不带）→ 视图取兵；忽略 Dead 标记（待销毁的当轮已计）。
    // P9：子弹也有 Collider → 用 Projectile 标志组件排除（防子弹被算作活兵 → 灭亡误判）。
    const auto& reg = registry_;
    int n = 0;
    for (auto e : reg.view<comp::FactionId, comp::Collider>()) {
        if (reg.all_of<comp::Dead>(e)) continue;
        if (reg.all_of<comp::Projectile>(e)) continue;
        if (reg.get<comp::FactionId>(e).value == factionId) ++n;
    }
    return n;
}

int Simulation::countEffects(int factionId) const {
    // 特效带 EffectTypeId；特效销毁直接 reg.destroy（无 Dead 标记）。
    const auto& reg = registry_;
    int n = 0;
    for (auto e : reg.view<comp::FactionId, comp::EffectTypeId>()) {
        if (reg.get<comp::FactionId>(e).value == factionId) ++n;
    }
    return n;
}

void Simulation::detectAnnihilationAndUnification() {
    // 防空：默认 Simulation（未 init）factions 为空 → 直接跳过（tick 冒烟测试依赖此，
    // 与 EconomySystem 同款防线）。
    if (factions_.size() != static_cast<size_t>(kFactionTotal)) return;
    // 灭亡判定（思路 6.0.5：城市/兵力/效果全灭才算灭亡）：
    // 在 tick 末尾（特效结算、死亡处理之后）执行 → 残留地雷/激光/爆炸的势力不误报。
    // 置 alive=false 会冻结该势力经济/产兵——灭亡势力无城市不可恢复，属预期（P4 基线 hash 更新）。
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
        Faction& f = factions_[static_cast<size_t>(id)];
        if (!f.alive) continue;
        if (f.cityCount == 0 && countArmies(id) == 0 && countEffects(id) == 0) {
            f.alive = false;
            events_.push_back(
                GameEvent{GameEventKind::FactionAnnihilated, id, 0,
                          formatEventTime(tickCount_, config_.sim.tickRate) + " 势力 "
                              + factionShortName(id) + " 被灭亡",
                          tickCount_});
        }
    }
    // 统一判定：alive 势力（1..8）恰剩一个 → 发 Unification（恰一次，unificationEmitted_ 防重）。
    if (unificationEmitted_) return;
    int winner = 0;
    for (int id = 1; id <= kPlayerFactionCount; ++id) {
        if (factions_[static_cast<size_t>(id)].alive) {
            if (winner != 0) return;  // 多于一个 alive → 未统一
            winner = id;
        }
    }
    if (winner != 0) {
        unificationEmitted_ = true;
        // P11：统一时刻截止统计（此后 record* 全部忽略，冻结）。
        stats_.cutoffTick = tickCount_;
        events_.push_back(
            GameEvent{GameEventKind::Unification, winner, 0,
                      formatEventTime(tickCount_, config_.sim.tickRate) + " 势力 "
                          + factionShortName(winner) + " 统一天下",
                      tickCount_});
    }
}

}  // namespace lw
