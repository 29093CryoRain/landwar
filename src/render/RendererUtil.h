// RendererUtil.h — 渲染器公共绘制辅助（2026-08 工程改进）。
// 六个渲染器（Map/Army/Effect/Projectile/City/CityMarker）共享的绘制原子，此前各自
// 复制实现（如 MapRenderer/CityRenderer 各有一份 pickSizeIndex/scaled，Army/Effect/
// Projectile 各有 max(2, lround(size*zoom))）。本文件收敛：
//   - spriteSize：     基础像素 × zoom，下限 2px 防退化
//   - scaledColor：    颜色按比例缩放（col × rate，0 → 黑；山/城图标/玩家指示调暗共用）
//   - isVisibleOnScreen：视野剔除（实体渲染器共用）
// LOD 档位选择见 TintCache::pickSizeIndex（同一重复点，归入数据类）。
// 纯头文件（内联），无新 TU。
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "render/Camera.h"

namespace lw::render {

// 精灵绘制尺寸 = basePx × zoom，下限 2px（防退化；Army/Effect/Projectile 同款）。
inline int spriteSize(double basePx, double zoom) {
    return std::max(2, static_cast<int>(std::lround(basePx * zoom)));
}

// 单通道按比例缩放（col × rate，四舍五入；与 mix_color(色, 黑, rate) 语义一致：
// 0 → 黑、1 → 原色）。山（0.08）/城图标（iconDarken）/玩家指示（0.8）共用。
inline std::array<int, 3> scaledColor(const std::array<int, 3>& col, double rate) {
    std::array<int, 3> out;
    for (int i = 0; i < 3; ++i)
        out[static_cast<size_t>(i)] =
            static_cast<int>(col[static_cast<size_t>(i)] * rate + 0.5);
    return out;
}

// 视野剔除：世界点 (wx,wy) 外扩 worldRadius（世界单位 U）后是否可能与视口相交
// （屏幕空间矩形重叠判定；y 翻转无影响）。worldRadius 取绘制尺寸折算的 U 数
// （sprite 半宽 + 余量）即可，多画无害、少画为 bug → 宁可保守。
inline bool isVisibleOnScreen(const Camera& cam, double wx, double wy, double worldRadius) {
    const double sx = cam.toScreenX(wx);
    const double sy = cam.toScreenY(wy);
    const double px = worldRadius * cam.cellPx();
    return sx + px >= 0.0 && sx - px <= static_cast<double>(cam.windowWidth()) &&
           sy + px >= 0.0 && sy - px <= static_cast<double>(cam.windowHeight());
}

}  // namespace lw::render
