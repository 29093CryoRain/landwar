// TintMath.h — 单像素着色数学（纯逻辑、头文件内联、可单测）。
// 统一贴图管线（TintCache 逐像素调用）两种模式：
//   1. Multiply（tintPixel）：白/灰源图 → 目标色（白→目标色、黑→黑、中间灰→目标色×亮度）。
//      玩家标记（白底 ring/arrow）用；`preserveWhiteAbove` 可保留近白（多色源图的白色核心）。
//   2. HueRotate（hueRotatePixel）：**彩色源图按色相旋转**到目标色——精确复现"色相处理"的原图
//      （RGB→HSV→h 移位→RGB，保留明度与饱和度；白色像素 s=0 天然不动 → 激光白芯自动保留）。
//      军队精灵（army_base.png 源列0 是红）用。灰度 luma 方案已废弃（会压暗、且破坏跨列亮度一致）。
//   3. 双色模板（decomposeTwoTonePixel/compositeTwoTone，视觉工程改进 ⑫）：
//      "红势力成品渲染"逐像素反解 → 模板 {v,m,Δ,α}，按任意势力 主色/副色 重合成。
//      副色=浅灰191 时与 HueRotate 逐像素等价（新管线是旧管线的严格推广，见文档 §2 ⑫）。
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

// 红族像素 (v,m,m) 可拆为 主色比例 rFrac = v−m（0..255）+ 灰度 g = m（0..255）；
// 灰度再对基准灰 refGray 拆 副色/白/黑 比例。合成公式见 compositeTwoTone。
// 量化：Δ 0..255 ↔ 0..360°（±0.7° 误差，视觉无感）。
struct TwoToneTemplate {
    int v = 0;      // 明度 max 通道（0..255）
    int m = 0;      // 灰度 min 通道（0..255）
    int delta = 0;  // 距纯红的色相偏移（0..255 ↔ 0..360°；灰像素 0）
    int a = 0;      // alpha（0..255）
};

// ---- 双色模板：反解（源 = 红势力成品渲染）----
inline TwoToneTemplate decomposeTwoTonePixel(int r, int g, int b, int a) {
    if (a <= 0) return TwoToneTemplate{};
    const int maxc = std::max(r, std::max(g, b));
    const int minc = std::min(r, std::min(g, b));
    const int delta =
        (maxc == minc) ? 0 : static_cast<int>(std::lround(hueOf(r, g, b) * 255.0 / 360.0));
    return TwoToneTemplate{maxc, minc, delta, a};
}

// ---- 双色模板：按势力 主色/副色 合成 ----
// base = (rFrac/255)·Pc + grayPart；grayPart 按灰度 g 对 refSecondaryGray 拆：
//   g ≥ ref  → 副色 + 白；g < ref → 副色（黑比例隐式，贡献 0）。
// 最后按 Δ 旋转色相（保留源像素相对纯红的色相偏移）。α 原样。
// 性质：合成(反解(p), 红, 浅灰191) ≡ p（±1~2/通道）；Pc.max=255 且 Sc=灰191 时 ≡ 旧 HueRotate。
inline TintRgba compositeTwoTone(const TwoToneTemplate& t,
                                 const std::array<int, 3>& primary,
                                 const std::array<int, 3>& secondary,
                                 int refSecondaryGray = 191) {
    if (t.a <= 0 || t.v <= 0) return TintRgba{0, 0, 0, t.a};
    const int rFrac = t.v - t.m;  // 主色比例（0..255）
    const int g = t.m;            // 灰度（0..255）
    // 灰度部分比例。
    double sec = 0.0, white = 0.0;
    if (g >= refSecondaryGray) {
        const int span = 255 - refSecondaryGray;
        if (span > 0) {
            sec = static_cast<double>(255 - g) / span;
            white = static_cast<double>(g - refSecondaryGray) / span;
        } else {
            sec = 1.0;
        }
    } else if (refSecondaryGray > 0) {
        sec = static_cast<double>(g) / refSecondaryGray;
    }
    const auto ch = [&](int pc, int sc) -> int {
        const double v = static_cast<double>(rFrac) / 255.0 * pc + sec * sc + white * 255.0;
        return v < 0.0 ? 0 : (v > 255.0 ? 255 : static_cast<int>(v + 0.5));
    };
    const int br = ch(primary[0], secondary[0]);
    const int bg = ch(primary[1], secondary[1]);
    const int bb = ch(primary[2], secondary[2]);
    if (t.delta == 0 || (br == bg && bg == bb)) return TintRgba{br, bg, bb, t.a};
    return hueRotatePixel(br, bg, bb, t.a, static_cast<double>(t.delta) * 360.0 / 255.0);
}

}  // namespace lw::render
