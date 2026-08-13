// Map.h — 地图网格 + BMP 解析 + 首都放置 + 城市注册表（翻新计划 §2.1 / §3.1 world/）。
// 语义必须与原版 mmap 完全一致（含屏幕朝向：世界 y=0 在屏幕底，见翻新计划 §2.1）。
// P13（思路 9.1）：城市从单格 bool 改为多格基建地块 + 全局注册表（cityId），等级幂律采样。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"

namespace lw {

class Snapshot;  // replay/Snapshot.h：读档需重建网格/首都/城市（Phase 6）

// 城市（P13 多格重构）：注册表存于 Map，cityId = 注册表下标，全图唯一、永不回收
// （城市一经放置整局存在，仅易主不销毁 → 下标稳定，无需空闲列表）。
// 基建格集合由 (baseX, baseY, w, h) 隐式导出（for dy in [0,h) for dx in [0,w)），不存格列表。
struct City {
    int id = -1;               // cityId（Map 注册表下标）
    int ownerId = 0;           // 归属势力 id（0 = 中立/未占）
    int level = 1;             // 等级 {1,2,4,6,9}
    int baseX = 0, baseY = 0;  // 锚点（形状左上格，世界坐标；固定朝向，不旋转）
    int w = 1, h = 1;          // 形状 列×行（由 level 查 config.shapes 推导）
    int lastProduceArmyN = 0;  // 产兵计数（保留原语义；公平调度 least-recent 排序键）
    std::uint64_t lastCapturedTick = 0;  // 最近一次整城易主/建立 tick（P15 首都"最久未被攻破"）
    // 城市中心（产兵/玩家指示锚点）。
    double centerX() const { return baseX + w / 2.0; }
    double centerY() const { return baseY + h / 2.0; }
};

// 单个网格单元（原版 Map 类）。
struct MapCell {
    int x = 0;
    int y = 0;
    int belongi = 0;    // 归属势力 id，0 = 中立
    bool land = false;  // false = 海, true = 陆
    bool mountain = false;  // 山（P5；陆地子集：land=true 且 mountain=true）
    // 可产城格（P5 改版）：陆地且源 g>probFloor（基图允许该格成为城市，即 ramp(g)>0）。
    // 仅 init 时由 loadFromBmp 设置、placeCapitals 使用；不进快照（快照不序列化本字段）。
    bool cityAllowed = false;
    // P13：所属城市 id（Map 注册表下标）；-1 = 非城市基建地块。取代旧 bool city。
    int cityId = -1;
};

class Map {
    friend class Snapshot;  // 读档直接重建 cells_/capitals_/cities_（Phase 6）

public:
    Map() = default;

    // 从配置拷贝尺寸，并预分配网格（全海）。
    void configure(const Config::Map& cfg);
    // 地形基图参数（P5 改版）：海陆阈值 + 概率 ramp + 移动规则（默认值即 Config::Terrain 默认）。
    void setTerrain(const Config::Terrain& t) { terrain_ = t; }
    const Config::Terrain& terrain() const { return terrain_; }
    // P13：城市等级形状表（默认值即 Config::City 内置表）。
    void setCityConfig(const Config::City& c) { cityConfig_ = c; }
    const Config::City& cityConfig() const { return cityConfig_; }

    // 方案 A：按原版逐字节解析 24bit BMP（54 字节头 + 每行 width*3+1 填充 + BGR + 自底向上）。
    // 地形基图编码（P5 改版）：任一分量 < terrain.seaChannelMin → 海（不掷骰）；
    // 否则 → 陆。**两遍读取**（P13）：第一遍读全图通道确定海/陆并**置全图 cell.land**
    //（确定性，不掷骰）——必须先行，否则第二遍放置多格形状时要检查的"未来格"（同行右侧/
    // 下方行）land 仍是 clear 默认 false，放置恒失败（2026-08-07 修，多格城全部塌缩为 1 级）；
    // 第二遍按原迭代序（y 先行后列）逐格掷山/城骰并放置城市形状。
    // 每陆格掷山骰一次（恒）；城概率仅在未被既有城市形状占用的格上掷（占用格跳过，
    // 不掷城概率，RNG 位置确定性）；城概率通过 → 先采样等级、再尝试放置，失败回退 1 级。
    // ramp(x) = clamp((x - probFloor) / probScale, 0, 1)。海/陆确定性、山/城概率性（种子相关）。
    // 失败返回 false。
    bool loadFromBmp(const std::string& path, Rng& rng);

    // 邻海修正（P5）：任何山格 8 邻域（含对角）有海格 → 置为普通陆。纯循环、无 RNG、确定性。
    void correctMountainCoast();

    // 放置 8 个首都：优先落在"可产城"格（cityAllowed，基图允许城市）；可产城格不足/太挤时
    // 放宽到任意陆地（保证游戏可玩）。随机陆地格，与已放置首都欧氏距离 >= capitalMinDistance。
    // 每个 attempt 消耗 get(width-1) + get(height-1)。重试耗尽（病态地图）返回 false。
    // P13：锚点格已是城市（loadFromBmp 放置的形状）→ 该城即首都（不新建）；否则注册 1 级城。
    bool placeCapitals(Rng& rng);

    // P13：finalize 重写——城市数 = 注册表大小（城市实体数，非格子数）。基建格全在陆地
    // （放置时校验），无海上城市；原清理步骤随之消失。旧经济公式 totalCities 语义变为
    // "城市实体数比"（P14 整体替换经济公式）。
    void finalize();

    MapCell& at(int x, int y);
    const MapCell& at(int x, int y) const;

    int width() const { return width_; }
    int height() const { return height_; }
    // 全图城市实体数（P13：注册表大小；旧语义为格子数，现已重构）。
    int totalCities() const { return static_cast<int>(cities_.size()); }
    int capitalX(int index) const { return capitalX_[static_cast<size_t>(index)]; }
    int capitalY(int index) const { return capitalY_[static_cast<size_t>(index)]; }
    int capitalCount() const { return static_cast<int>(capitalX_.size()); }
    int blockSize() const { return blockSize_; }
    int panelWidth() const { return panelWidth_; }

    // ---- 城市注册表（P13）----
    // 等级采样（幂律，思路 9.1）：s = alpha + beta，P(L=1)=1-2^-s; P(L=2)=2^-s-4^-s; ...
    // u = rng.unit()（1 次 RNG）按累计概率定等级。纯函数，公开以便单测各分支。
    static int sampleCityLevel(const Config::City& cc, Rng& rng);
    int cityCount() const { return static_cast<int>(cities_.size()); }
    const std::vector<City>& cities() const { return cities_; }
    const City& city(int id) const { return cities_[static_cast<size_t>(id)]; }
    City& city(int id) { return cities_[static_cast<size_t>(id)]; }
    // 注册新城市（锚点格 baseX/baseY，形状由 level 查 cityConfig_ 推导）。占用/陆地/可成城
    // 校验由调用方先行完成（loadFromBmp 经 canPlaceCity；placeCapitals 自身保证）。返回新 cityId。
    int addCity(int level, int baseX, int baseY);
    // 锚点 (baseX,baseY) 处放置 level 级城的形状占用检查：界内 ∧ 锚点可成城 ∧ 全部基建格
    // 陆地（含山）∧ 无重叠（cityId==-1）。
    bool canPlaceCity(int level, int baseX, int baseY) const;

private:
    void clear();

    int width_ = 105;
    int height_ = 95;
    int blockSize_ = 15;
    int panelWidth_ = 600;
    int capitalMinDistance_ = 28;
    Config::Terrain terrain_;   // 地形基图参数（P5 改版）
    Config::City cityConfig_;   // 城市等级形状表（P13；setCityConfig 设置）

    std::vector<MapCell> cells_;  // index = y*width_ + x
    std::vector<int> capitalX_;
    std::vector<int> capitalY_;
    std::vector<City> cities_;  // 城市注册表（P13）
};

}  // namespace lw
