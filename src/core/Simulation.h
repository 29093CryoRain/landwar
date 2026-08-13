// Simulation.h — 游戏模拟核心（纯逻辑，无任何渲染/平台依赖）。
// 翻新计划 §3：所有全局状态收敛到本类；无头模式/回放/存档的前提。
// Phase 3: 持有 Map + factions + Rng + entt registry + 空间哈希 + ttime + go_sea 概率；
//          tick() 已接入军队阶段（移动+战斗+死亡）。
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <entt/entt.hpp>

#include "core/Config.h"
#include "core/Options.h"
#include "core/Random.h"
#include "sim/SpatialHash.h"
#include "sim/Statistics.h"
#include "sim/components.h"
#include "world/Faction.h"
#include "world/Map.h"

namespace lw {

// 游戏事件（开发计划 P4，思路 6.0.5）：重大事件通用通道。
// 纯展示：不进快照（与 PlayerIntent 同理）；Application 每帧 takeEvents() → MessagePanel。
enum class GameEventKind {
    FactionAnnihilated,  // 势力灭亡（城市+兵力+特效全灭）
    Unification,         // 统一（只剩一个 alive 势力）
    TechAcquired,        // 取得科技（P8）
    CapitalLost,         // 正式首都沦陷（P15：整城易主含首都 → 发消息）
    Custom,              // 自定义
};

struct GameEvent {
    GameEventKind kind = GameEventKind::Custom;
    int factionId = 0;      // 相关势力（TechAcquired 为取得方）
    int data = 0;           // 附加量（如科技 id / 等级）
    std::string text;       // 组装好的文案（渲染层直接显示）
    std::uint64_t tick = 0; // 产生时的逻辑帧
};

// 玩家产兵意图（P2）：外部输入，不消耗 Rng、不进快照。
// active/factionId 由 Application 每帧维护（读 Faction.aiId）；selectedType/cityX/cityY
// 为玩家选择（兵种键/城市点击）。PlayerAI 消费它。
// cityX/cityY 为城市【几何中心】世界坐标（可为 .5 等小数；2026-08-07 用户要求选取坐标落在
// 产兵城中央——产兵本身就在中心，选取也须以中心比对；旧版存锚点格 baseX/baseY，中心与
// 锚点混淆）。比较用容差（中心匹配），不用 ==。
struct PlayerIntent {
    bool active = false;         // 本局是否有玩家操控
    int factionId = 0;           // 玩家操控的势力 id
    int selectedType = -1;       // 选定兵种（ArmyType 枚举值）；-1 = 未选
    double cityX = -1.0, cityY = -1.0;  // 选定产兵城中心（世界坐标）；<0 = 未选
};

class Snapshot;  // replay/Snapshot.h：存档/读档需替换全部内部状态（Phase 6）

class Simulation {
    friend class Snapshot;

public:
    Simulation();
    explicit Simulation(const Config& config, std::uint32_t seed);
    // P6 RNG 分离：mapSeed 驱动地图生成（山/城骰子，loadFromBmp）；seed（主种子）驱动
    // 首都放置与之后一切。旧两参构造等价 mapSeed=seed（headless/CLI 兼容）。
    Simulation(const Config& config, std::uint32_t seed, std::uint32_t mapSeed);

    // 加载地图 -> 放置首都 -> 清理统计 -> 建势力（含回合顺序 shuffle 与征服首都）。
    // RNG 消耗顺序见翻新计划 §1.4（地图骰子走 mapRng_、首都/后续走 rng_）。失败（地图缺失等）返回 false。
    bool init();

    // 带选项初始化（开发计划 P1）：options 决定势力出场与产兵 AI（Faction.aiId）。
    // 等价 init() + 设置 options_；禁用势力不征服首都（首都保持中立）。RNG 序列与全启用一致
    // （init 阶段 conquer 的 freeArmyEnabled=false，不消耗 Rng），确定性键 = (seed, config, map, options)。
    bool init(const Options& options);

    // 单步推进模拟（60Hz 逻辑帧）。顺序见翻新计划 §2.10。
    void tick();

    // 征服 (x,y) 归势力 factionId（供 Movement/Effect 系统调用，见翻新计划 §2.8）。
    // freeArmyEnabled=false：跳过势力8 免费产兵（init 阶段用，去掉开局免费兵；用户定夺 2026-08）。
    void conquer(int x, int y, int factionId, bool freeArmyEnabled = true);

    std::uint64_t tickCount() const { return tickCount_; }
    const Config& config() const { return config_; }
    const Options& options() const { return options_; }
    // P6：地图随机种子（主面板显示；Snapshot 序列化值，mapRng_ 状态不序列化——init 后不再消费）。
    std::uint32_t mapSeed() const { return mapSeed_; }
    // 玩家产兵意图（P2）：外部输入，不进快照；headless/回放无意图 → 玩家势力不产兵。
    void setPlayerIntent(const PlayerIntent& intent) { playerIntent_ = intent; }
    const PlayerIntent& playerIntent() const { return playerIntent_; }
    Rng& rng() { return *rng_; }
    const Rng& rng() const { return *rng_; }
    // 测试钩子：替换 RNG 实现（如 MockRng 固定 chance 分支）。
    void setRng(std::unique_ptr<Rng> rng) { rng_ = std::move(rng); }
    // 测试钩子：直接设定逻辑帧计数（经济公式单测等用；Phase 6 读档恢复时间时也会用到）。
    void setTickCount(std::uint64_t n) { tickCount_ = n; }

    Map& map() { return map_; }
    const Map& map() const { return map_; }
    Faction& faction(int id) { return factions_[static_cast<size_t>(id)]; }
    const Faction& faction(int id) const { return factions_[static_cast<size_t>(id)]; }
    const std::vector<Faction>& factions() const { return factions_; }
    std::vector<Faction>& factions() { return factions_; }
    const std::vector<int>& turnOrder() const { return turnOrder_; }
    std::vector<int>& turnOrder() { return turnOrder_; }  // 经济阶段洗牌回合顺序用
    const std::vector<PendingSpawn>& pendingSpawns() const { return pendingSpawns_; }
    std::vector<PendingSpawn>& pendingSpawns() { return pendingSpawns_; }

    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    SpatialHash& spatialHash() { return spatialHash_; }
    const SpatialHash& spatialHash() const { return spatialHash_; }
    double goSeaProbability() const { return goSeaProbability_; }
    std::vector<DeathEvent>& deaths() { return deaths_; }
    const std::vector<DeathEvent>& deaths() const { return deaths_; }
    // P11 统计（纯计数；系统经 sim.stats() 记录击杀/占领/产兵）。
    Statistics& stats() { return stats_; }
    const Statistics& stats() const { return stats_; }
    // 取出并清空死亡事件（DeathSystem 使用）。
    std::vector<DeathEvent> takeDeaths();

    // P4 事件通道：压入事件（系统/检测产生；纯展示，不进快照）。
    void pushEvent(GameEvent ev) { events_.push_back(std::move(ev)); }
    // 取出并清空事件（Application 每帧 drain 到 MessagePanel）。
    std::vector<GameEvent> takeEvents();
    // P15：玩家/AI 指定候补新都（external input 修改游戏状态，随 CapitalState 入快照）。
    // 仅无正式首都时可用（有正式首都 → 不动作）；城市须己方。指定后由 CapitalSystem 在窗口
    // 期满（或 immediateRelocate 态下）执行正式迁都。返回是否生效。
    bool setDesignatedCapital(int factionId, int cityId);
    // P8：玩家科研三选一结算（external input；techIndex=-1=跳过，否则须在候选内才应用）。
    // 结算：清空科技点 + 阈值提升 + 清除待选（跳过同样消耗本次科研机会）。返回是否生效。
    bool choosePlayerTech(int techIndex);
    // P4 灭亡/统一检测：tick 末尾调用（也暴露给单测直接调用）。纯逻辑，不消耗 Rng。
    // 灭亡：alive && cityCount==0 && 无兵 && 无特效 → 置 alive=false + FactionAnnihilated。
    // 统一：alive 势力数（1..8）==1 → 恰一次 Unification。
    void detectAnnihilationAndUnification();
    // 该势力当前活兵数 / 特效数（灭亡判定用；registry 视图统计，不消耗 Rng）。
    int countArmies(int factionId) const;
    int countEffects(int factionId) const;

    // 创建实体：实体 id 单调递增（不依赖 entt 空闲列表回收）→ 快照读档后 id 分配完全确定，
    // 继续模拟与直跑逐帧一致（Phase 9 修：读档后新实体 id 分歧导致 RNG/级联漂移）。
    entt::entity createEntity() { return registry_.create(entt::entity{nextEntityId_++}); }

private:
    void initFactions();
    void processPendingSpawns();

    Config config_;
    Options options_;            // 菜单选项（P1；headless/默认 = 全启用 aiId=0）
    PlayerIntent playerIntent_;  // 玩家产兵意图（P2；不进快照）
    std::unique_ptr<Rng> rng_;   // 主种子（首都放置 + 一切后续）
    std::unique_ptr<Rng> mapRng_;  // 地图种子（P6：仅 init 时 loadFromBmp 的山/城骰子用）
    std::uint32_t mapSeed_ = 0;    // 地图种子值（显示/快照用）
    Map map_;
    std::vector<Faction> factions_;
    std::vector<int> turnOrder_;  // 0..7 排列（原版 faction_solve_order）
    std::vector<PendingSpawn> pendingSpawns_;  // 势力8 免费兵待办（tick 末落地）
    std::vector<DeathEvent> deaths_;
    entt::registry registry_;
    SpatialHash spatialHash_;
    double goSeaProbability_ = 1.0;  // 原版 go_sea_probability
    std::uint64_t tickCount_ = 0;    // 原版 ttime
    std::uint32_t nextEntityId_ = 1; // 实体 id 单调分配器（见 createEntity 注）
    // P4 事件通道：events_ 纯展示（不进快照）；unificationEmitted_ 防重复发统一事件。
    std::vector<GameEvent> events_;
    bool unificationEmitted_ = false;
    // P11：统计（击杀/占领/产兵，截止到统一）。序列化入快照。
    Statistics stats_;
};

}  // namespace lw
