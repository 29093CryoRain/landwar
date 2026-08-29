// Map.h — 地图网格 + BMP 解析 + 首都放置 + 城市注册表（翻新计划 §2.1 / §3.1 world/）。
// 语义必须与原版 mmap 完全一致（含屏幕朝向：世界 y=0 在屏幕底，见翻新计划 §2.1）。
// P13（思路 9.1）：城市从单格 bool 改为多格基建地块 + 全局注册表（cityId），等级幂律采样。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "world/tiling/Tiling.h"

namespace lw {

class Snapshot;  // replay/Snapshot.h：读档需重建网格/首都/城市（Phase 6）

// 城市（P13 多格重构）：注册表存于 Map，cityId = 注册表下标，全图唯一、永不回收
// （城市一经放置整局存在，仅易主不销毁 → 下标稳定，无需空闲列表）。
// P12：基建格集合由 锚点格（baseIndex）+ 形状（level 查 cfg.city.setFor(tiling)）隐式导出
// （形状 = 相对锚格中心的世界偏移格集，见 Config::City::Shape），取代矩形 (baseX, baseY, w, h)。
struct City {
    int id = -1;               // cityId（Map 注册表下标）
    int ownerId = 0;           // 归属势力 id（0 = 中立/未占）
    double level = 1.0;        // 等级（当前严格等于城市基建区域面积）
    double area = 1.0;         // 城市基建区域面积（预留字段；当前 area == level）
    int baseX = 0, baseY = 0;  // 锚点坐标（方：(x,y)；六/三：(c,r)；快照序列化）
    int baseIndex = -1;        // 锚点格下标（P12；快照序列化）
    int shapeVariant = 0;      // 形状表变体下标（同级多形状时放置选定，快照序列化；
                               // 渲染/读档用同一变体保证形状一致；旧档默认 0）。
    int w = 1, h = 1;          // 形状 AABB 格数（方：w×h 语义不变；六/三：AABB 列/行，显示用）
    int lastProduceArmyN = 0;  // 产兵计数（保留原语义；公平调度 least-recent 排序键）
    std::uint64_t lastCapturedTick = 0;  // 最近一次整城易主/建立 tick（P15 首都"最久未被攻破"）
    double centerX_ = 0, centerY_ = 0;   // 形状几何中心（世界坐标；产兵/玩家指示锚点）
    double centerX() const { return centerX_; }
    double centerY() const { return centerY_; }
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
    // P12：lwmap 加载（六/三角地形基图；自描述格式，见 MapGenerator）。通道语义与 BMP
    // 一致 → 与 loadFromBmp 共用第二遍（掷骰/放置城市）。失败返回 false。
    bool loadFromLwmap(const std::string& path, Rng& rng);
    // 格坐标 (c, r) → 格下标（P12；方 = r*width+c；六 = r*cols+c；三 = 正三角锚）。
    int cellIndexAt(int c, int r) const;
    // 带基础格序号的版本（半正/Laves 用；方/六忽略 b，三 = b 取 0 正/1 反）。
    int cellIndexAt(int c, int r, int b) const;

    // 邻海修正（P5）：任何山格 8 邻域（含对角）有海格 → 置为普通陆。纯循环、无 RNG、确定性。
    void correctMountainCoast();

    // 放置 8 个首都：优先落在"可产城"格（cityAllowed，基图允许城市）；可产城格不足/太挤时
    // 放宽到任意陆地（保证游戏可玩）。随机陆地格，与已放置首都欧氏距离 >= capitalMinDistance。
    // 每个 attempt 消耗 get(width-1) + get(height-1)。重试耗尽（病态地图）返回 false。
    // P13：锚点格已是城市（loadFromBmp 放置的形状）→ 该城即首都（不新建）；否则注册 1 级城。
    bool placeCapitals(Rng& rng);


    MapCell& at(int x, int y);
    const MapCell& at(int x, int y) const;
    // P12：按格下标取格（平铺数组下标，见 Tiling.h 头注；非方形路径统一用）。
    MapCell& atIndex(int idx);
    const MapCell& atIndex(int idx) const;
    // P12：密铺几何（世界范围/中心/取格/邻接/穿越）。
    TilingType tiling() const { return geom_.type; }
    const TilingGeom& geom() const { return geom_; }
    double worldWidth() const { return geom_.worldWidth(); }
    double worldHeight() const { return geom_.worldHeight(); }
    int cellCount() const { return geom_.cellCount(); }
    void cellCenter(int idx, double& wx, double& wy) const { geom_.cellCenter(idx, wx, wy); }

    int width() const { return width_; }
    int height() const { return height_; }
    // 全图城市实体数（P13：注册表大小；旧语义为格子数，现已重构）。
    int totalCities() const { return static_cast<int>(cities_.size()); }
    int capitalX(int index) const { return capitalX_[static_cast<size_t>(index)]; }
    int capitalY(int index) const { return capitalY_[static_cast<size_t>(index)]; }
    // 表驱动密铺的首都基础格 b（每周期域内基础格序号；方/六/三恒 0）。
    int capitalB(int index) const { return capitalB_[static_cast<size_t>(index)]; }
    int capitalCount() const { return static_cast<int>(capitalX_.size()); }

    // ---- 城市注册表（P13/P12）----
    // 等级采样（幂律，思路 9.1）：s = alpha + beta，P(L=1)=1-2^-s; P(L=2)=2^-s-4^-s; ...
    // u = rng.unit()（1 次 RNG）按累计概率定等级。纯函数，公开以便单测各分支。
    // set = 该密铺的等级集（cfg.city.setFor(tiling)）。
    static double sampleCityLevel(const Config::City& cc, const Config::City::TilingSet& set,
                                  Rng& rng);
    // 兼容重载：正方形等级集（旧签名，测试用）。
    static double sampleCityLevel(const Config::City& cc, Rng& rng) {
        return sampleCityLevel(cc, cc.square, rng);
    }
    int cityCount() const { return static_cast<int>(cities_.size()); }
    const std::vector<City>& cities() const { return cities_; }
    const City& city(int id) const { return cities_[static_cast<size_t>(id)]; }
    City& city(int id) { return cities_[static_cast<size_t>(id)]; }
    // P12：城市基建格下标集合（形状 = 锚点 + level 查密铺形状表，经世界偏移解析）。
    std::vector<int> cityCells(const City& c) const;
    // P12：形状 → 基建格下标（level + 锚点格 index + 变体 variant；界外格 = -1）。
    // 公开供放置/渲染/测试。variant < 0 时按锚点确定性选取（旧行为，测试用）。
    std::vector<int> shapeCells(double level, int anchorIndex, int variant = -1) const;
    // P12：城市形状几何中心（世界坐标；= 全部基建格中心平均；方 = baseX+w/2 同旧式）。
    void cityCenter(const City& c, double& wx, double& wy) const;
    // P12：重算全部城市的 baseIndex/AABB/几何中心（快照读档后调用）。
    void recomputeCityGeometry();
    // 注册新城市（锚点格 index，形状由 level + tiling 推导）。占用/陆地/可成城校验由
    // 调用方先行完成（loadFromBmp 经 canPlaceCity；placeCapitals 自身保证）。返回新 cityId。
    // rng 非空且同级多形状（变体>1）时：从"能放下的变体"中随机选（保证同级不同形状
    // 都能出现，2026-08-17 修复：旧逻辑总选第一个变体 → 后续变体永不出现，如 Laves31212
    // 的菱形 6 级城）。rng 为空 → 第一个能放下的变体（测试/兼容路径）。
    int addCity(double level, int index, Rng* rng = nullptr);
    // 兼容重载：正方形 (baseX, baseY) 锚点（旧签名，测试用；等价 index = baseY*width+baseX）。
    int addCity(double level, int baseX, int baseY) { return addCity(level, baseY * width_ + baseX); }
    // 锚点 (index) 处放置 level 级城的形状占用检查：界内 ∧ 锚点可成城 ∧ 全部基建格
    // 陆地（含山）∧ 无重叠（cityId==-1）。P12：形状不可跨环绕接缝（界内即含此约束）。
    bool canPlaceCity(double level, int index) const;
    // 兼容重载：正方形 (baseX, baseY) 锚点（旧签名，测试用）。
    bool canPlaceCity(double level, int baseX, int baseY) const {
        return canPlaceCity(level, baseY * width_ + baseX);
    }

private:
    void updateCityGeometry(City& city);
    // canPlaceCity 的实现；requireAllowed=false 时跳过"锚点可成城"校验（首都回退到任意陆地）。
    bool canPlaceCityImpl(double level, int index, bool requireAllowed) const;
    // 返回 index 处放置 level 级城时第一个能放下的变体下标（-1 = 无变体可放）。
    // 与 canPlaceCityImpl 判定一致（同密铺同形状集），供 addCity 选定并持久化到 City。
    int placeableVariant(double level, int index, bool requireAllowed) const;
    // 返回全部能放下的变体下标（2026-08-17：addCity 用它 + rng 随机选，保证同级不同
    // 形状都能出现；placeableVariant = 本列表首元素或 -1）。
    std::vector<int> placeableVariants(double level, int index, bool requireAllowed) const;
    // P1.2：将形状表 cells（世界偏移）解析为格下标；方/六/三/半正/Laves 统一。
    // 三角反锚镜像 dy；界外格返回 -1。shapeCells 与 placeableVariants 共用。
    std::vector<int> resolveShapeCells(const Config::City::Shape& shape, int anchorIndex) const;
    // 每格概率通道（地形基图：海/陆确定性 + 山 R / 城 G 概率通道）。方/六/三共用。
    struct CellChannels {
        bool sea;
        int g;
        int r;
    };
    void clear();
    // 第二遍（方/六/三共用）：按格下标序掷山/城骰 + 放置城市（RNG 顺序 = 格下标序）。
    void finishTerrain(const std::vector<CellChannels>& ch, Rng& rng);

    int width_ = 105;
    int height_ = 95;
    int capitalMinDistance_ = 28;
    Config::Terrain terrain_;   // 地形基图参数（P5 改版）
    Config::City cityConfig_;   // 城市等级形状表（P13/P12；setCityConfig 设置）
    TilingGeom geom_;           // 密铺几何（P12；configure 设置）

    std::vector<MapCell> cells_;  // index = y*width_ + x
    std::vector<int> capitalX_;
    std::vector<int> capitalY_;
    std::vector<int> capitalB_;   // 首都基础格序号（半正/Laves；方/六/三 = 0）
    std::vector<City> cities_;  // 城市注册表（P13）
};

}  // namespace lw
