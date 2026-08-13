// Random.h — 确定性随机数（mt19937）。get/chance 使用**无取模偏差**的标准拒绝采样 /
// [0,1) 均匀（不再镜像 DxLib 取模语义）。同 (seed, config, map) → 唯一可复现；
// Python 黄金数据脚本（tools/gen_golden_map.py）复刻相同算法独立验证地图解析。
// 供新代码用的现代 API：unit()/range()/randomSeed()。
#pragma once

#include <cstdint>
#include <random>
#include <string>

namespace lw {

// 原版 get_rand_angle 离散档数（14447），历史遗留注记——现用 range(0, 2π) 连续均匀（见 MathUtil）。

class Rng {
public:
    Rng() = default;
    explicit Rng(std::uint32_t s) { seed(s); }

    void seed(std::uint32_t s);
    std::uint32_t seedValue() const { return seed_; }

    // 均匀整数 [0, n]（含端点）。**无取模偏差**（阈值拒绝法，portable，Python 黄金数据可复刻）。
    int get(int n);

    // 概率判断：uniform [0,1) < p（double 精度）。
    // virtual：便于单测 mock 固定分支（Phase 3 下海概率、Phase 2 势力8 免费兵）。
    virtual bool chance(double probability);

    // ---- 现代补充（新代码用；可自由扩展）----
    // virtual：便于单测 mock 固定 u（P13 等级采样各分支，同 chance 的模式）。
    virtual double unit();               // 均匀 [0, 1)
    double range(double lo, double hi);  // 均匀 [lo, hi)

    // 现代随机种子：时间驱动（每次启动不同；显式 --seed 仍确定性，见 Cli::seedSet）。
    static std::uint32_t randomSeed();

    // 可序列化状态（存档/回放用，Phase 6）。
    std::string state() const;
    void setState(const std::string& s);

private:
    std::mt19937 mt_{};
    std::uint32_t seed_ = 0;
};

}  // namespace lw
