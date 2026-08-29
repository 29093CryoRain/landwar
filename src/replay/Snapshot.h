// Snapshot.h — Simulation 全状态序列化（存档/读档，翻新计划 §3.1 replay/ + Phase 6）。
// 快照必须取在 tick 之间（死亡/待产兵队列已清空、无 Dead 实体，见 §2.10），保证往返无损。
// 因模拟完全确定（seed+config+map 决定一切），回放 = 同种子重跑；存档用于中途分支/继续。
#pragma once

#include <string>

namespace lw {

class Simulation;

class Snapshot {
public:
    // 序列化 sim 全状态 → JSON 文本（config/map/factions/registry/rng/ttime/goSea/回合顺序）。
    static std::string serialize(const Simulation& sim);

    // 从 JSON 文本恢复 sim（覆盖全部状态；sim 可为默认构造的空实例，之后可直接 tick）。
    // 失败返回 false 并在 *err（若有）写原因。
    static bool deserialize(Simulation& sim, const std::string& json, std::string* err = nullptr);

    static bool saveToFile(const Simulation& sim, const std::string& path,
                           std::string* err = nullptr);
    static bool loadFromFile(Simulation& sim, const std::string& path,
                             std::string* err = nullptr);

private:
    static bool deserializeInto(Simulation& sim, const std::string& json,
                                std::string* err);
};

}  // namespace lw
