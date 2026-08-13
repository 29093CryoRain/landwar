// TintMath.h — 单像素着色数学（纯逻辑、头文件内联、可单测）。
// 统一贴图管线（TintCache 逐像素调用）两种模式：
//   1. Multiply（tintPixel）：白/灰源图 → 目标色（白→目标色、黑→黑、中间灰→目标色×亮度）。
//      玩家标记（白底 ring/arrow）用；`preserveWhiteAbove` 可保留近白（多色源图的白色核心）。
//   2. HueRotate（hueRotatePixel）：**彩色源图按色相旋转**到目标色——精确复现"色相处理"的原图
//      （RGB→HSV→h 移位→RGB，保留明度与饱和度；白色像素 s=0 天然不动 → 激光白芯自动保留）。
//      军队精灵（army_base.png 源列0 是红）用。灰度 luma 方案已废弃（会压暗、且破坏跨列亮度一致）。
#pragma once

#include <array>
#include <cmath>

namespace lw::render {

// 逐像素结果（0..255）。
struct TintRgba {
    int r = 0, g = 0, b = 0, a = 0;
};

// ---- Multiply：源色 × 目标色/255（grayscaleFirst 时源用亮度 lum）----
inline TintRgba tintPixel(int r, int g, int b, int a, const std::array<int, 3>& target,
                          bool grayscaleFirst, int preserveWhiteAbove) {
    if (a <= 0) return TintRgba{0, 0, 0, 0};
    const int lum = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
    if (preserveWhiteAbove > 0 && lum >= preserveWhiteAbove) {
        // 保留近白：源灰度则保 (lum,lum,lum)，否则保原色。
        return TintRgba{grayscaleFirst ? lum : r, grayscaleFirst ? lum : g,
                        grayscaleFirst ? lum : b, a};
    }
    const int sr = grayscaleFirst ? lum : r;
    const int sg = grayscaleFirst ? lum : g;
    const int sb = grayscaleFirst ? lum : b;
    return TintRgba{sr * target[0] / 255, sg * target[1] / 255, sb * target[2] / 255, a};
}

// ---- 色相（度 [0,360)）；灰（max==min）色相无关，返回 0 ----
inline double hueOf(int r, int g, int b) {
    const int maxc = std::max(r, std::max(g, b));
    const int minc = std::min(r, std::min(g, b));
    if (maxc == minc) return 0.0;
    double h = 0.0;
    if (maxc == r)
        h = 60.0 * std::fmod((g - b) / static_cast<double>(maxc - minc), 6.0);
    else if (maxc == g)
        h = 60.0 * ((b - r) / static_cast<double>(maxc - minc) + 2.0);
    else
        h = 60.0 * ((r - g) / static_cast<double>(maxc - minc) + 4.0);
    if (h < 0) h += 360.0;
    return h;
}

// ---- HueRotate：把像素色相平移 deltaDeg（保留明度/饱和度；白像素不动）----
inline TintRgba hueRotatePixel(int r, int g, int b, int a, double deltaDeg) {
    if (a <= 0) return TintRgba{0, 0, 0, 0};
    const int maxc = std::max(r, std::max(g, b));
    const int minc = std::min(r, std::min(g, b));
    const double v = maxc / 255.0;
    const double s = (maxc == 0) ? 0.0 : (maxc - minc) / static_cast<double>(maxc);
    double h = std::fmod(hueOf(r, g, b) + deltaDeg + 360.0, 360.0);
    // HSV → RGB
    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;
    double rp = 0.0, gp = 0.0, bp = 0.0;
    if (h < 60) {
        rp = c; gp = x;
    } else if (h < 120) {
        rp = x; gp = c;
    } else if (h < 180) {
        gp = c; bp = x;
    } else if (h < 240) {
        gp = x; bp = c;
    } else if (h < 300) {
        rp = x; bp = c;
    } else {
        rp = c; bp = x;
    }
    return TintRgba{static_cast<int>((rp + m) * 255.0 + 0.5),
                    static_cast<int>((gp + m) * 255.0 + 0.5),
                    static_cast<int>((bp + m) * 255.0 + 0.5), a};
}

}  // namespace lw::render
