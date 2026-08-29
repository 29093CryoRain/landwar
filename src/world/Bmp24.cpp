#include "world/Bmp24.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <type_traits>

namespace lw {

namespace {

void setError(std::string* err, const std::string& message) {
    if (err) *err = message;
}

template <typename T>
T readLe(const unsigned char* bytes) {
    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<U>(bytes[i]) << (8 * i);
    return static_cast<T>(value);
}

template <typename T>
void writeLe(unsigned char* bytes, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xffu);
}

bool validDimensions(int width, int height) {
    return width > 0 && height > 0 && width <= 100000 && height <= 100000;
}

}  // namespace

bool readBmp24(const std::string& path, Bmp24Image& image, std::string* err) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        setError(err, "cannot open BMP '" + path + "'");
        return false;
    }

    unsigned char header[54]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        setError(err, "BMP header truncated");
        return false;
    }
    if (header[0] != 'B' || header[1] != 'M') {
        setError(err, "BMP signature is not BM");
        return false;
    }
    const std::uint32_t pixelOffset = readLe<std::uint32_t>(header + 10);
    const std::uint32_t dibSize = readLe<std::uint32_t>(header + 14);
    const std::int32_t signedWidth = readLe<std::int32_t>(header + 18);
    const std::int32_t signedHeight = readLe<std::int32_t>(header + 22);
    const std::uint16_t planes = readLe<std::uint16_t>(header + 26);
    const std::uint16_t bitsPerPixel = readLe<std::uint16_t>(header + 28);
    const std::uint32_t compression = readLe<std::uint32_t>(header + 30);
    if (dibSize < 40 || signedWidth <= 0 || signedHeight == 0 || planes != 1
        || bitsPerPixel != 24 || compression != 0
        || !validDimensions(signedWidth, signedHeight < 0 ? -signedHeight : signedHeight)
        || pixelOffset < 54) {
        setError(err, "unsupported BMP format (expected positive width, 24-bit BI_RGB)");
        return false;
    }

    const int width = signedWidth;
    const int height = signedHeight < 0 ? -signedHeight : signedHeight;
    const bool topDown = signedHeight < 0;
    const std::size_t stride = (static_cast<std::size_t>(width) * 3u + 3u) & ~std::size_t{3};
    if (pixelOffset > 54) file.seekg(static_cast<std::streamoff>(pixelOffset), std::ios::beg);
    if (!file) {
        setError(err, "BMP pixel offset is invalid");
        return false;
    }

    Bmp24Image result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width) * height);
    std::vector<unsigned char> row(stride);
    for (int fileRow = 0; fileRow < height; ++fileRow) {
        file.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
        if (file.gcount() != static_cast<std::streamsize>(row.size())) {
            setError(err, "BMP pixel data truncated");
            return false;
        }
        const int y = topDown ? height - 1 - fileRow : fileRow;
        for (int x = 0; x < width; ++x) {
            const std::size_t src = static_cast<std::size_t>(x) * 3;
            result.pixels[static_cast<std::size_t>(y) * width + x] = {row[src + 2], row[src + 1],
                                                                        row[src]};
        }
    }
    image = std::move(result);
    return true;
}

bool writeBmp24(const std::string& path, int width, int height,
                const std::vector<std::array<unsigned char, 3>>& pixels, std::string* err) {
    if (!validDimensions(width, height)
        || pixels.size() != static_cast<std::size_t>(width) * height) {
        setError(err, "invalid BMP dimensions or pixel count");
        return false;
    }
    const std::size_t stride = (static_cast<std::size_t>(width) * 3u + 3u) & ~std::size_t{3};
    std::vector<unsigned char> header(54, 0);
    header[0] = 'B';
    header[1] = 'M';
    writeLe<std::uint32_t>(header.data() + 2,
                           static_cast<std::uint32_t>(54u + stride * static_cast<std::size_t>(height)));
    writeLe<std::uint32_t>(header.data() + 10, 54u);
    writeLe<std::uint32_t>(header.data() + 14, 40u);
    writeLe<std::int32_t>(header.data() + 18, width);
    writeLe<std::int32_t>(header.data() + 22, height);
    writeLe<std::uint16_t>(header.data() + 26, 1u);
    writeLe<std::uint16_t>(header.data() + 28, 24u);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        setError(err, "cannot open BMP '" + path + "' for writing");
        return false;
    }
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    std::vector<unsigned char> row(stride, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto& rgb = pixels[static_cast<std::size_t>(y) * width + x];
            const std::size_t dst = static_cast<std::size_t>(x) * 3;
            row[dst] = rgb[2];
            row[dst + 1] = rgb[1];
            row[dst + 2] = rgb[0];
        }
        file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        std::fill(row.begin(), row.end(), 0);
    }
    if (!file) {
        setError(err, "BMP write failed");
        return false;
    }
    return true;
}

}  // namespace lw
