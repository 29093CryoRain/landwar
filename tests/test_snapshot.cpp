// test_snapshot.cpp — 无头/存档/回放（翻新计划 §4 Phase 6）。
// Config 往返、快照全状态往返、实体 id 保留、存档→继续与直接继续逐 tick 一致、
// CLI 解析、headless 摘要确定性、（存档→继续）==（直接跑）端到端。
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/Config.h"
#include "core/Simulation.h"
#include "replay/Cli.h"
#include "replay/Headless.h"
#include "replay/Snapshot.h"
#include "sim/components.h"
#include "TestUtil.h"

namespace {

using namespace lw;

// 跑 N tick 的真实模拟（含地图/首都/经济产兵，产生战损与实体回收）。
Simulation makeSim(std::uint32_t seed, int ticks) {
    Simulation sim(lwtest::loadCfg(), seed);
    EXPECT_TRUE(sim.init());
    for (int i = 0; i < ticks; ++i) sim.tick();
    return sim;
}

// registry 全量确定性转储（兵 + 特效，**按实体 id 升序**），用于状态严格比较。
// P9 起系统按实体 id 确定性迭代（存储序无关），直跑/读档的 raw 视图序不同 → 比较须按 id 排序。
std::string registryDump(const Simulation& sim) {
    std::ostringstream oss;
    {
        std::vector<entt::entity> ents;
        for (auto e : sim.registry().view<comp::Position, comp::Collider>())
            ents.push_back(e);
        std::sort(ents.begin(), ents.end(),
                  [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
        for (auto e : ents) {
            if (sim.registry().all_of<comp::Projectile>(e)) continue;  // 子弹无 OnLand（P9 回归陷阱）
            const auto& p = sim.registry().get<comp::Position>(e);
            const auto& v = sim.registry().get<comp::Velocity>(e);
            const auto& s = sim.registry().get<comp::Speed>(e);
            const auto& o = sim.registry().get<comp::OnLand>(e);
            const auto& f = sim.registry().get<comp::FactionId>(e);
            const auto& u = sim.registry().get<comp::UnitType>(e);
            const auto& c = sim.registry().get<comp::Collider>(e);
            const auto& l = sim.registry().get<comp::LandHistory>(e);
            oss << "A" << entt::to_integral(e) << "(" << p.x << "," << p.y << "," << v.angle << ","
                << s.value << "," << o.value << "," << f.value << "," << static_cast<int>(u.type)
                << "," << c.radius << "," << l.lastLandTime << ","
                << sim.registry().all_of<comp::Dead>(e) << ");";
        }
    }
    {
        std::vector<entt::entity> ents;
        for (auto e : sim.registry().view<comp::EffectTypeId>())
            ents.push_back(e);
        std::sort(ents.begin(), ents.end(),
                  [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
        for (auto e : ents) {
            const auto& p = sim.registry().get<comp::Position>(e);
            const auto& f = sim.registry().get<comp::FactionId>(e);
            const auto& t = sim.registry().get<comp::EffectTypeId>(e);
            const auto& tm = sim.registry().get<comp::EffectTimer>(e);
            const auto& pr = sim.registry().get<comp::EffectParams>(e);
            const auto& cr = sim.registry().get<comp::Creator>(e);
            oss << "E" << entt::to_integral(e) << "(" << p.x << "," << p.y << "," << f.value << ","
                << static_cast<int>(t.type) << "," << tm.createdTick << "," << pr.p0 << "," << pr.p1
                << "," << pr.p2 << "," << cr.x << "," << cr.y << "," << cr.factionId << ");";
        }
    }
    return oss.str();
}

// 每 tick 廉价指纹（rng 状态哈希 + 可见计数），用于继续模拟的逐步对比。
std::uint64_t simFingerprint(const Simulation& sim) {
    std::uint64_t h = fnv1a64(sim.rng().state());
    auto mix = [&h](std::uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix(sim.tickCount());
    mix(static_cast<std::uint64_t>(sim.goSeaProbability() * 1e9));
    std::uint64_t armies = 0, effects = 0;
    for (auto e : sim.registry().view<comp::Position, comp::Collider>())
        if (!sim.registry().all_of<comp::Dead>(e)) ++armies;
    for ([[maybe_unused]] auto e : sim.registry().view<comp::EffectTypeId>()) ++effects;
    mix(armies);
    mix(effects);
    std::uint64_t landSum = 0, citySum = 0;
    double ecoSum = 0;
    for (int id = 1; id <= 8; ++id) {
        const auto& f = sim.faction(id);
        landSum += static_cast<std::uint64_t>(f.landCount);
        citySum += static_cast<std::uint64_t>(f.cityCount);
        ecoSum += f.economy;
    }
    mix(landSum);
    mix(citySum);
    mix(static_cast<std::uint64_t>(ecoSum * 1e9));
    return h;
}

// ---- Config 序列化往返 ----

TEST(Config, ToJsonRoundTrip) {
    Config cfg = lwtest::loadCfg();
    const std::string j1 = cfg.toJson();
    Config back = Config::loadFromJson(j1);
    EXPECT_EQ(back.toJson(), j1);  // 键对称：解析再序列化逐字节一致
}

TEST(Config, ToJsonRoundTripDefaults) {
    Config cfg = Config::loadFromJson("{}");  // 全默认 + 内置势力表
    Config back = Config::loadFromJson(cfg.toJson());
    EXPECT_EQ(back.toJson(), cfg.toJson());
}

// ---- 快照全状态往返 ----

TEST(Snapshot, RoundTripPreservesState) {
    const Simulation sim = makeSim(42, 150);
    const std::string json = Snapshot::serialize(sim);
    Simulation r;
    ASSERT_TRUE(Snapshot::deserialize(r, json));

    EXPECT_EQ(r.config().toJson(), sim.config().toJson());
    EXPECT_EQ(r.tickCount(), sim.tickCount());
    EXPECT_DOUBLE_EQ(r.goSeaProbability(), sim.goSeaProbability());
    EXPECT_EQ(r.turnOrder(), sim.turnOrder());
    // pendingSpawns 无 operator==，逐字段比较。
    ASSERT_EQ(r.pendingSpawns().size(), sim.pendingSpawns().size());
    for (std::size_t i = 0; i < sim.pendingSpawns().size(); ++i) {
        EXPECT_EQ(r.pendingSpawns()[i].type, sim.pendingSpawns()[i].type);
        EXPECT_EQ(r.pendingSpawns()[i].factionId, sim.pendingSpawns()[i].factionId);
        EXPECT_DOUBLE_EQ(r.pendingSpawns()[i].x, sim.pendingSpawns()[i].x);
        EXPECT_DOUBLE_EQ(r.pendingSpawns()[i].y, sim.pendingSpawns()[i].y);
    }

    // P6：地图种子值随快照往返（读档后主面板显示）。
    EXPECT_EQ(r.mapSeed(), sim.mapSeed());

    // map
    EXPECT_EQ(r.map().width(), sim.map().width());
    EXPECT_EQ(r.map().height(), sim.map().height());
    EXPECT_EQ(r.map().totalCities(), sim.map().totalCities());
    EXPECT_EQ(r.map().capitalCount(), sim.map().capitalCount());
    for (int i = 0; i < sim.map().capitalCount(); ++i) {
        EXPECT_EQ(r.map().capitalX(i), sim.map().capitalX(i));
        EXPECT_EQ(r.map().capitalY(i), sim.map().capitalY(i));
    }
    // P13：城市注册表逐城对比（id/ownerId/level/形状/产兵计数/易主 tick）。
    ASSERT_EQ(r.map().cities().size(), sim.map().cities().size());
    for (std::size_t j = 0; j < sim.map().cities().size(); ++j) {
        const auto& ca = sim.map().cities()[j];
        const auto& cb = r.map().cities()[j];
        EXPECT_EQ(ca.id, cb.id) << "city " << j;
        EXPECT_EQ(ca.ownerId, cb.ownerId) << "city " << j;
        EXPECT_EQ(ca.level, cb.level) << "city " << j;
        EXPECT_EQ(ca.baseX, cb.baseX) << "city " << j;
        EXPECT_EQ(ca.baseY, cb.baseY) << "city " << j;
        EXPECT_EQ(ca.w, cb.w) << "city " << j;
        EXPECT_EQ(ca.h, cb.h) << "city " << j;
        EXPECT_EQ(ca.lastProduceArmyN, cb.lastProduceArmyN) << "city " << j;
        EXPECT_EQ(ca.lastCapturedTick, cb.lastCapturedTick) << "city " << j;
    }
    for (int y = 0; y < sim.map().height(); ++y) {
        for (int x = 0; x < sim.map().width(); ++x) {
            const auto& a = sim.map().at(x, y);
            const auto& b = r.map().at(x, y);
            EXPECT_EQ(a.belongi, b.belongi) << "(" << x << "," << y << ")";
            EXPECT_EQ(a.land, b.land) << "(" << x << "," << y << ")";
            EXPECT_EQ(a.cityId, b.cityId) << "(" << x << "," << y << ")";
            EXPECT_EQ(a.mountain, b.mountain) << "(" << x << "," << y << ")";
        }
    }

    // factions
    ASSERT_EQ(r.factions().size(), sim.factions().size());
    for (std::size_t i = 0; i < sim.factions().size(); ++i) {
        const auto& a = sim.factions()[i];
        const auto& b = r.factions()[i];
        EXPECT_EQ(a.id, b.id) << "fid " << i;
        EXPECT_EQ(a.alive, b.alive) << "fid " << i;
        EXPECT_EQ(a.color, b.color) << "fid " << i;
        EXPECT_EQ(a.cityCount, b.cityCount) << "fid " << i;
        EXPECT_EQ(a.landCount, b.landCount) << "fid " << i;
        EXPECT_EQ(a.numArmyProduced, b.numArmyProduced) << "fid " << i;
        EXPECT_DOUBLE_EQ(a.economy, b.economy) << "fid " << i;
        EXPECT_DOUBLE_EQ(a.freeArmyChance, b.freeArmyChance) << "fid " << i;
        EXPECT_EQ(a.armyCost, b.armyCost) << "fid " << i;
        EXPECT_EQ(a.producedCost, b.producedCost) << "fid " << i;  // 公平调度堆累计花费
        ASSERT_EQ(a.cityIds.size(), b.cityIds.size()) << "fid " << i;
        for (std::size_t j = 0; j < a.cityIds.size(); ++j) {
            EXPECT_EQ(a.cityIds[j], b.cityIds[j]) << "fid " << i << " cityIds[" << j << "]";
        }
    }

    // registry + rng
    EXPECT_EQ(registryDump(r), registryDump(sim));
    EXPECT_EQ(r.rng().state(), sim.rng().state());
}

TEST(Snapshot, MapSeedRoundTripsAndContinuationMatches) {
    // P6：显式 mapSeed 随快照往返；读档续跑与直跑一致（mapRng_ 不参与续跑，主 rng 状态即一切）。
    Simulation direct(lwtest::loadCfg(), 42, 7);
    ASSERT_TRUE(direct.init());
    for (int i = 0; i < 150; ++i) direct.tick();
    EXPECT_EQ(direct.mapSeed(), 7u);

    Simulation loaded;
    ASSERT_TRUE(Snapshot::deserialize(loaded, Snapshot::serialize(direct)));
    EXPECT_EQ(loaded.mapSeed(), 7u);
    EXPECT_EQ(loaded.rng().state(), direct.rng().state());
    for (int i = 0; i < 150; ++i) {
        direct.tick();
        loaded.tick();
        EXPECT_EQ(simFingerprint(loaded), simFingerprint(direct)) << "tick " << (150 + i);
    }
}

TEST(Snapshot, EntityIdsPreserved) {
    const Simulation sim = makeSim(42, 150);  // 有战损/回收 → 版本位可能非零
    // P9：系统按 id 迭代、读档存储序与直跑不可复现 → 比较按 id 升序（不依赖 raw 视图序）。
    std::vector<std::uint32_t> armyIds, effIds;
    for (auto e : sim.registry().view<comp::Position, comp::Collider>())
        armyIds.push_back(entt::to_integral(e));
    for (auto e : sim.registry().view<comp::EffectTypeId>())
        effIds.push_back(entt::to_integral(e));
    std::sort(armyIds.begin(), armyIds.end());
    std::sort(effIds.begin(), effIds.end());

    Simulation r;
    ASSERT_TRUE(Snapshot::deserialize(r, Snapshot::serialize(sim)));

    std::vector<std::uint32_t> rArmyIds, rEffIds;
    for (auto e : r.registry().view<comp::Position, comp::Collider>())
        rArmyIds.push_back(entt::to_integral(e));
    for (auto e : r.registry().view<comp::EffectTypeId>())
        rEffIds.push_back(entt::to_integral(e));
    std::sort(rArmyIds.begin(), rArmyIds.end());
    std::sort(rEffIds.begin(), rEffIds.end());
    EXPECT_EQ(armyIds, rArmyIds);
    EXPECT_EQ(effIds, rEffIds);
}

// ---- 存档→继续 与 直接继续 逐 tick 一致 ----

TEST(Snapshot, ContinuationMatchesDirect) {
    Simulation direct = makeSim(42, 200);
    const std::string json = Snapshot::serialize(direct);
    Simulation loaded;
    ASSERT_TRUE(Snapshot::deserialize(loaded, json));
    EXPECT_EQ(loaded.tickCount(), direct.tickCount());
    EXPECT_EQ(registryDump(loaded), registryDump(direct));

    // 再跑 300 tick，每 tick 对比指纹（rng 状态 + 全部可见状态）。
    // 注：续跑后两 registry 回收实体的"版本位"会不同（读档用全新 registry 重建），
    // 因此不能直接比序列化 JSON（含实体 id）；比指纹已覆盖 rng 状态与全部可见状态。
    constexpr int kMore = 300;
    for (int i = 0; i < kMore; ++i) {
        direct.tick();
        loaded.tick();
        EXPECT_EQ(simFingerprint(loaded), simFingerprint(direct))
            << "tick " << (200 + i + 1);
    }
    EXPECT_EQ(simFingerprint(loaded), simFingerprint(direct));
}

// 存档文件往返（saveToFile → loadFromFile）。
TEST(Snapshot, FileRoundTrip) {
    const Simulation sim = makeSim(42, 50);
    const std::string path = "test_snapshot_file_roundtrip.json";
    std::string err;
    ASSERT_TRUE(Snapshot::saveToFile(sim, path, &err)) << err;
    Simulation r;
    ASSERT_TRUE(Snapshot::loadFromFile(r, path, &err)) << err;
    EXPECT_EQ(registryDump(r), registryDump(sim));
    EXPECT_EQ(r.tickCount(), sim.tickCount());
    std::remove(path.c_str());
    // 文件不存在应失败并给出原因。
    Simulation r2;
    std::string err2;
    EXPECT_FALSE(Snapshot::loadFromFile(r2, "no_such_file.json", &err2));
    EXPECT_FALSE(err2.empty());
}

// P13/P15：v4 起旧版存档作废（P13 MapCell.cityId 重构；P15 v5 加 CapitalState）；旧版本拒绝读入。
TEST(Snapshot, OldVersionRejected) {
    Simulation r;
    std::string err;
    EXPECT_FALSE(Snapshot::deserialize(r, R"({"version":3})", &err));
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("unsupported version"), std::string::npos);
}

// ---- CLI 解析 ----

TEST(Cli, ParsesFlags) {
    const char* argv[] = {"landwar", "--headless", "--seed", "7", "--ticks", "100",
                          "--config", "a.json",    "--map",   "b.bmp",
                          "--save",   "s.json",    "--summary"};
    const auto o = parseCli(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    EXPECT_TRUE(o.headless);
    EXPECT_TRUE(o.summary);
    EXPECT_EQ(o.seed, 7u);
    EXPECT_EQ(o.ticks, 100);
    EXPECT_EQ(o.configPath, "a.json");
    EXPECT_EQ(o.mapPath, "b.bmp");
    EXPECT_EQ(o.savePath, "s.json");
    EXPECT_TRUE(o.loadPath.empty());
}

TEST(Cli, ReplaySetsSeedAndHeadless) {
    const char* argv[] = {"landwar", "--replay", "123"};
    const auto o = parseCli(3, const_cast<char**>(argv));
    EXPECT_TRUE(o.headless);
    EXPECT_EQ(o.seed, 123u);

    const char* argv2[] = {"landwar", "--replay"};
    const auto o2 = parseCli(2, const_cast<char**>(argv2));
    EXPECT_TRUE(o2.headless);
    EXPECT_EQ(o2.seed, 42u);
}

TEST(Cli, SimFlagsImplyHeadless) {
    const char* argv[] = {"landwar", "--seed", "9"};
    const auto o = parseCli(3, const_cast<char**>(argv));
    EXPECT_TRUE(o.headless);  // 任何模拟控制参数都进入无头

    const char* argv2[] = {"landwar"};
    const auto o2 = parseCli(1, const_cast<char**>(argv2));
    EXPECT_FALSE(o2.headless);  // 无参数 → 窗口模式
}

TEST(Cli, ScreenshotFlagStaysWindowed) {
    // --screenshot 是窗口模式 QA 钩子：不强制无头（截图需要窗口/渲染器）。
    const char* argv[] = {"landwar", "--screenshot", "out.png", "--ticks", "600"};
    const auto o = parseCli(5, const_cast<char**>(argv));
    EXPECT_EQ(o.screenshotPath, "out.png");
    EXPECT_EQ(o.ticks, 600);
    EXPECT_FALSE(o.headless);
}

// ---- headless 摘要确定性 + 端到端存档分支 ----

TEST(Headless, SummaryDeterministic) {
    auto run = [](std::uint32_t seed) {
        const Simulation sim = makeSim(seed, 300);
        return formatSummary(sim, seed);
    };
    const std::string s1 = run(42);
    const std::string s2 = run(42);
    EXPECT_EQ(s1, s2);  // 同种子 → 摘要逐字节一致（含 state_hash）
    EXPECT_NE(s1.find("state_hash="), std::string::npos);
    EXPECT_NE(s1.find("  total land="), std::string::npos);
    const std::string s3 = run(43);
    EXPECT_NE(s1, s3);  // 不同种子 → 摘要不同（state_hash 捕获状态差异）
}

// P10：wrap 开同种子确定性 + 与关态不同。wrap 消耗 0 RNG（反弹 bounceJitter 消耗 1）→
// wrap 开产生新的确定性轨迹（state_hash 与关态不同）；同种子两次逐字节一致。
// P13：地图/轨迹更新后 300 tick 内可能无兵到界（wrap 未触发 → 开关态相同），改动态扫描：
// 两 sim 并排推进，用 simFingerprint（廉价：rng 状态哈希 + 计数）找首个分歧 tick，证明 wrap
// 确实生效；分歧 tick 随地图变化而移动，不硬编码。分歧点两侧 state_hash 必不同。
TEST(Headless, WrapDeterministicAndDiffersFromOff) {
    auto run = [](bool wrap, std::uint32_t seed, int ticks) {
        Options o;
        o.map.wrap = wrap;
        Simulation sim(lwtest::loadCfg(), seed);
        EXPECT_TRUE(sim.init(o));
        for (int i = 0; i < ticks; ++i) sim.tick();
        return formatSummary(sim, seed);
    };
    // 同种子 → wrap 开摘要逐字节一致。
    EXPECT_EQ(run(true, 42, 300), run(true, 42, 300));
    // 动态找首个分歧 tick（wrap 开不消耗反弹 RNG → 首次到界即与关态分叉）。
    constexpr int kMaxScan = 8000;
    int divergeTick = -1;
    {
        Options oOn, oOff;
        oOn.map.wrap = true;
        oOff.map.wrap = false;
        Simulation on(lwtest::loadCfg(), 42), off(lwtest::loadCfg(), 42);
        ASSERT_TRUE(on.init(oOn));
        ASSERT_TRUE(off.init(oOff));
        for (int t = 1; t <= kMaxScan; ++t) {
            on.tick();
            off.tick();
            if (simFingerprint(on) != simFingerprint(off)) { divergeTick = t; break; }
        }
    }
    ASSERT_GT(divergeTick, 0) << kMaxScan << " tick 内 wrap 开关态未分歧（wrap 未生效？）";
    // 分歧点处两侧 state_hash 不同（wrap 开 ≠ 关；同种子各自确定）。
    EXPECT_NE(run(true, 42, divergeTick), run(false, 42, divergeTick));
}

// 回归（Phase 8 发现）：特效活跃时存档不再崩溃。Snapshot::serialize 曾用 Position 存储
// 遍历兵，但特效也用 Position → 一旦有活跃特效（20000 tick 确定性出现），
// get<Velocity> 触发 entt "Set does not contain entity" 断言。改为按兵专用存储 UnitType。
TEST(Snapshot, RoundTripWithActiveEffects) {
    // 动态找首个有活跃特效的 tick（模拟轨迹随行为调整而变化，不硬编码 tick）。
    constexpr int kMaxScan = 5000;
    int kTick = -1;
    {
        Simulation scan = makeSim(42, 0);
        for (int t = 0; t < kMaxScan; ++t) {
            scan.tick();
            int n = 0;
            for ([[maybe_unused]] auto e : scan.registry().view<comp::EffectTypeId>()) ++n;
            if (n > 0) { kTick = t + 1; break; }
        }
    }
    ASSERT_GT(kTick, 0) << "前 " << kMaxScan << " tick 未出现活跃特效（回归前提）";
    Simulation direct = makeSim(42, kTick);
    int effects = 0;
    for ([[maybe_unused]] auto e : direct.registry().view<comp::EffectTypeId>()) ++effects;
    ASSERT_GT(effects, 0) << "tick " << kTick << " 应存在活跃特效（回归前提）";

    // serialize 不再崩溃；往返后**当前状态**逐字节一致（registry 全兵/特效 + rng 状态）。
    const std::string json = Snapshot::serialize(direct);
    Simulation loaded;
    ASSERT_TRUE(Snapshot::deserialize(loaded, json));
    EXPECT_EQ(registryDump(loaded), registryDump(direct));
    EXPECT_EQ(loaded.rng().state(), direct.rng().state());

    // 续跑不崩溃 + 逐 tick 一致（P9 起系统按实体 id 确定性迭代 → 读档续跑与直跑位精确，
    // 见 SaveLoadContinuationMatchesDirect 与 probe 验证；此处只回归"特效活跃存档往返"）。
    constexpr int kMore = 100;
    for (int i = 0; i < kMore; ++i) {
        direct.tick();
        loaded.tick();
        EXPECT_EQ(simFingerprint(loaded), simFingerprint(direct)) << "tick " << (kTick + i + 1);
    }
}

TEST(Headless, SaveLoadContinuationMatchesDirect) {
    const std::string path = "test_snapshot_phase6_branch.json";

    // 直接跑 300 tick 的摘要（基准）。
    const Simulation direct = makeSim(42, 300);
    const std::string directSum = formatSummary(direct, 42);

    // 独立实例跑 200 tick → 存档。
    {
        const Simulation s = makeSim(42, 200);
        std::string err;
        ASSERT_TRUE(Snapshot::saveToFile(s, path, &err)) << err;
    }

    // 读档 → 再跑 100 tick。终局应等于直接跑 300。
    Simulation loaded;
    std::string err;
    ASSERT_TRUE(Snapshot::loadFromFile(loaded, path, &err)) << err;
    EXPECT_EQ(loaded.tickCount(), 200u);
    for (int i = 0; i < 100; ++i) loaded.tick();
    const std::string loadedSum = formatSummary(loaded, 42);

    EXPECT_EQ(loadedSum, directSum);  // 存档→继续 == 直接跑（state_hash 一致）
    std::remove(path.c_str());
}

}  // namespace
