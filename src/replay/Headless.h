// Headless.h — 无头 CLI 运行（翻新计划 Phase 6）。
// 构建（或读档）模拟 → 步进 N tick → 可选存档/摘要。回放 = 同种子重跑（--replay），
// 因模拟完全确定（seed+config+map 决定一切），见翻新计划 §2.10。
#pragma once

#include <cstdint>
#include <string>

namespace lw {

struct CliOptions;
class Simulation;

// 无头入口：按 opts 构建（或读档）模拟、步进、可选存档与摘要。返回进程退出码（0 成功）。
int runHeadless(const CliOptions& opts);

// 终局摘要文本（供 --summary，也供单测做确定性回归）。同种子输出逐字节一致（elapsed 单独日志）。
// 含 state_hash（对全状态序列化做 FNV-1a，任何状态漂移都会改变哈希）。
std::string formatSummary(const Simulation& sim, std::uint32_t seed);

// FNV-1a 64 位（确定性哈希；测试与 state_hash 共用）。
std::uint64_t fnv1a64(const std::string& s);

}  // namespace lw
