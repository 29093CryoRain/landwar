// Bmp24.h — 标准 24-bit 无压缩 BMP 读写（地图基图共用）。
#pragma once

#include <array>
#include <string>
#include <vector>

namespace lw {

struct Bmp24Image {
    int width = 0;
    int height = 0;
    // RGB，按游戏世界坐标从 y=0 到 y=height-1 存储。
    std::vector<std::array<unsigned char, 3>> pixels;
};

bool readBmp24(const std::string& path, Bmp24Image& image, std::string* err = nullptr);
bool writeBmp24(const std::string& path, int width, int height,
                const std::vector<std::array<unsigned char, 3>>& pixels,
                std::string* err = nullptr);

}  // namespace lw
