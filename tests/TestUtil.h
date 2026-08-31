// TestUtil.h — 单测共享工具。
#pragma once

#include <array>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/Config.h"
#include "core/Random.h"

namespace lwtest {

// 可控 Rng：chance() 返回预设结果，便于确定性分支测试（Phase 3 下海/反弹、Phase 2 势力8 免费兵）。
class MockRng final : public lw::Rng {
public:
    std::vector<bool> results;
    std::size_t next = 0;
    std::vector<double> units;
    std::size_t nextUnit = 0;
    bool chance(double) override { return next < results.size() ? results[next++] : false; }
    double unit() override { return nextUnit < units.size() ? units[nextUnit++] : 0.5; }
};

inline lw::Config loadCfg() {
    return lw::Config::loadFromFile("data/config.jsonc");
}

// 写一张 105×95 地形基图测试 BMP（P5 改版编码）：
//   默认普通陆 (128,128,128)；mountains 涂 (255,32,32)（r=255 → 确定性山）；
//   cityZones 涂 (32,200,32)（g=200 → 城概率≈0.57）；seas 涂 (0,0,0)。
// 与 C++ 读取器一致：54 头 + 标准 4 字节对齐行 + BGR + 自底向上。
// 用途：让地图相关测试不依赖 data/ 下随时可能被删除/改动的具体地图文件。
inline void writeTestMapBmp(const std::string& path,
                            const std::vector<std::pair<int, int>>& mountains,
                            const std::vector<std::pair<int, int>>& cityZones,
                            const std::vector<std::pair<int, int>>& seas = {}) {
    constexpr int W = 105, H = 95, ROWSIZE = (W * 3 + 3) & ~3;
    std::vector<unsigned char> data(static_cast<std::size_t>(54 + H * ROWSIZE), 0);
    data[0] = 'B';
    data[1] = 'M';
    const unsigned sz = 54 + H * ROWSIZE;
    std::memcpy(&data[2], &sz, 4);
    const unsigned off = 54;
    std::memcpy(&data[10], &off, 4);
    const unsigned ih = 40;
    std::memcpy(&data[14], &ih, 4);
    std::memcpy(&data[18], &W, 4);
    std::memcpy(&data[22], &H, 4);
    const unsigned short p = 1, bpp = 24;
    std::memcpy(&data[26], &p, 2);
    std::memcpy(&data[28], &bpp, 2);
    const auto put = [&](int x, int y, int r, int g, int b) {
        std::size_t o = static_cast<std::size_t>(54 + y * ROWSIZE + x * 3);
        data[o] = static_cast<unsigned char>(b);
        data[o + 1] = static_cast<unsigned char>(g);
        data[o + 2] = static_cast<unsigned char>(r);
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) put(x, y, 128, 128, 128);
    for (const auto& [x, y] : mountains) put(x, y, 255, 32, 32);
    for (const auto& [x, y] : cityZones) put(x, y, 32, 200, 32);
    for (const auto& [x, y] : seas) put(x, y, 0, 0, 0);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
}

}  // namespace lwtest
