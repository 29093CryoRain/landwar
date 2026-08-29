// Options.h — 菜单选项（开发计划 P1，思路 1）。
// 纯逻辑、可序列化：势力出场/产兵 AI 选择、地图选择、边界贯通开关（P10 预留）。
// 开始游戏时保存到 userdata/options.json（2026-08 工程改进：运行期产物入 userdata/），
// 启动读取作为初始化选项（确定性键含 options，见计划 §0）。
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/GameDefs.h"
#include "core/Paths.h"

namespace lw {

// 单个面板的布局（开发计划 P3 面板系统）：唯一 id + 可见性 + 位置/大小。
// 可移动面板随 options.json 持久化，重启读回（主面板固定，不入持久化）。
struct PanelLayout {
    std::string id;
    bool visible = false;
    float x = 0, y = 0, w = 0, h = 0;
};

// 单个势力的出场配置（Options.factions 下标 0..7 ↔ 势力 id 1..8）。
struct FactionSlot {
    bool enabled = true;  // 是否出场（false → 不放置首都、alive=false，见计划 P1 任务 4）
    int aiId = 0;         // 产兵 AI id：0=默认 AI（公平调度堆）；1=玩家（P2 生效，最多一个）
};

// 地图选择。
struct MapSelection {
    enum class Kind { File, Random };  // Random = P6 随机图
    Kind kind = Kind::File;
    std::string file = kDefaultMapFile;  // Kind::File：地图文件路径（默认=基线图）
    std::uint32_t randomSeed = 0;       // 地图随机种子（P6，独立于主种子）：驱动随机图生成 +
                                        // 所有地图的山/城骰子（loadFromBmp）；菜单选图页可编辑
    // Kind::Random：随机图参数（P6，MapGenerator）。
    int width = 105;                    // 随机图长（默认与基线一致）
    int height = 95;                    // 随机图宽
    double seaRatio = 0.40;             // 海占比目标（0 = 全陆地）
    double mountainDensity = 0.08;      // 内陆山占比目标
    double cityDensity = 0.02;          // 城占比目标（占陆地格）
     bool forceCoast = false;            // 强制边缘为海（P6 随机图）：边缘带削减海拔，最外一圈恒为海
    std::string tiling = "square";      // P12：密铺（square/hex/tri；随机图生成用；预装 BMP 恒方形）
};

struct Options {
    std::array<FactionSlot, kPlayerFactionCount> factions{};  // 下标 0..7 ↔ 势力 id 1..8
    MapSelection map;
    std::vector<PanelLayout> panels;  // 面板布局（P3；可移动面板 id/可见性/位置/大小）

    // 校验：最多一个势力 aiId=1（玩家操控）。返回 false 时 *err 写原因。
    bool validate(std::string* err = nullptr) const;

    // 与 Config 一致：缺键回退默认；toJson/loadFromJson 键严格对称（供单测往返）。
    static Options loadFromJson(const std::string& jsonText);
    static Options loadFromFile(const std::string& path);
    std::string toJson() const;
    void saveToFile(const std::string& path) const;
};

}  // namespace lw
