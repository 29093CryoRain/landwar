// FixedTimestep.h — 固定步长累加器（2026-08 工程改进）。
// 从 Application::run 抽出：帧时长 × 倍速 → 推进虚拟时间，消费整数个逻辑步。
// 纯逻辑、无平台依赖，可独立单测。语义与旧实现逐位一致：
//   - advance(dt)：acc += dt；超上限截断（防螺旋死亡，旧值 0.25s）。
//   - consumeTicks()：while (acc >= step) acc -= step, ++n；余数保留到下一帧。
// 暂停/单步由调用方在 consume 之外处理（本类不感知）。
#pragma once

namespace lw {

class FixedTimestep {
public:
    explicit FixedTimestep(double stepSeconds, double maxAccumSeconds = 0.25)
        : step_(stepSeconds), maxAccum_(maxAccumSeconds) {}

    // 推进虚拟时间（帧秒数 × 倍速）。
    void advance(double dtSeconds) {
        acc_ += dtSeconds;
        if (acc_ > maxAccum_) acc_ = maxAccum_;
    }

    // 本帧应执行的逻辑步数（每步 step_ 秒）。不消耗累计外的语义。
    int consumeTicks() {
        int n = 0;
        while (acc_ >= step_) {
            acc_ -= step_;
            ++n;
        }
        return n;
    }

    void reset() { acc_ = 0.0; }

    double stepSeconds() const { return step_; }

private:
    double step_;
    double maxAccum_;
    double acc_ = 0.0;
};

}  // namespace lw
