// TerrainCodec.h — 地形基图通道协议的唯一实现。
#pragma once

#include <algorithm>
#include <cmath>

#include "core/Config.h"

namespace lw::terrain {

inline bool isSea(int r, int g, int b, const Config::Terrain& config) {
    return std::min({r, g, b}) < config.seaChannelMin;
}

inline double ramp(int channel, const Config::Terrain& config) {
    if (channel <= config.probFloor) return 0.0;
    return std::clamp(static_cast<double>(channel - config.probFloor)
                          / static_cast<double>(config.probScale),
                      0.0, 1.0);
}

inline unsigned char encodeProbability(double probability, const Config::Terrain& config) {
    const int value = config.probFloor
                      + static_cast<int>(std::lround(config.probScale
                                                       * std::clamp(probability, 0.0, 1.0)));
    return static_cast<unsigned char>(std::clamp(value, 0, 255));
}

}  // namespace lw::terrain
