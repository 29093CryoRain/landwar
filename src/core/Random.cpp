#include "core/Random.h"

#include <chrono>
#include <cstdint>
#include <sstream>

namespace lw {

void Rng::seed(std::uint32_t s) {
    seed_ = s;
    mt_.seed(s);
}

int Rng::get(int n) {
    if (n < 0) return 0;  // 防御：GetRand 语义要求 n>=0
    // 无取模偏差：阈值拒绝法。v ∈ [0, 2^32) 均匀；limit = floor(2^32/range)*range 为 range 的
    // 倍数，拒绝 v>=limit 后 v%range 在 [0, n] 均匀。portable，Python 黄金数据脚本可精确复刻。
    const std::uint32_t range = static_cast<std::uint32_t>(n) + 1u;
    const std::uint64_t limit = (std::uint64_t{1} << 32) / range * range;
    std::uint32_t v;
    do {
        v = mt_();
    } while (v >= limit);
    return static_cast<int>(v % range);
}

bool Rng::chance(double probability) {
    return unit() < probability;
}

double Rng::unit() {
    // 单次抽取 → [0,1)（32 bit 精度，游戏足够）；免 uniform_real_distribution 对象开销。
    return static_cast<double>(mt_()) / 4294967296.0;
}

double Rng::range(double lo, double hi) {
    return lo + unit() * (hi - lo);
}

std::uint32_t Rng::randomSeed() {
    // 现代种子：单调时钟纳秒（每次运行不同）。std::random_device 在 MinGW 上可能退化为
    // 时间序列，直接用时钟更可靠。显式 --seed 走确定性路径，不经过这里。
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    const std::uint64_t t = static_cast<std::uint64_t>(ns);
    return static_cast<std::uint32_t>((t ^ (t >> 32)) & 0xFFFFFFFFu);
}

std::string Rng::state() const {
    std::ostringstream oss;
    oss << mt_;
    return oss.str();
}

void Rng::setState(const std::string& s) {
    std::istringstream iss(s);
    iss >> mt_;
}

}  // namespace lw
