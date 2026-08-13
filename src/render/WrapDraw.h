// WrapDraw.h — P10 边界贯通（环绕）双渲染辅助。
// 纯逻辑（不依赖 SDL/渲染器，仅数学），头文件内联 → 渲染器与单测共用（tests 只链 landwar_core，
// 内联函数无需链接即可调用）。世界坐标（格）→ 屏幕副本位列表。
#pragma once

#include <cmath>

namespace lw::render {

// 收集某兵需绘制的屏幕位置：主位置 + 贴界副本（最多 9 个 = 1 主 + 4 轴向 + 4 角上对角）。
//   wx, wy    — 兵的世界坐标（格）
//   edgeDist  — 贴界阈值（格）：世界坐标距图边 < edgeDist 时画对侧副本（= 绘制尺寸折格）
//   mapW, mapH— 图宽/高（格）
//   sx, sy    — 主位置的屏幕像素
//   spanX, spanY — 图宽/高（屏幕像素，正数）：副本 = 主位置 ± 跨度
//               （世界 y=0 在屏幕底 → 下副本 = sy - spanY、上副本 = sy + spanY）
//   out       — 输出数组（至少 [9][2]）；返回副本数（≥1，恒含主位置）。
inline int collectWrapDraws(double wx, double wy, double edgeDist, double mapW, double mapH,
                            int sx, int sy, int spanX, int spanY, int out[9][2]) {
    const bool nearLeft = wx < edgeDist;
    const bool nearRight = mapW - wx < edgeDist;
    const bool nearBottom = wy < edgeDist;
    const bool nearTop = mapH - wy < edgeDist;

    int n = 0;
    out[n][0] = sx;
    out[n][1] = sy;
    ++n;
    if (nearLeft) {
        out[n][0] = sx + spanX;
        out[n][1] = sy;
        ++n;
    }
    if (nearRight) {
        out[n][0] = sx - spanX;
        out[n][1] = sy;
        ++n;
    }
    if (nearBottom) {
        out[n][0] = sx;
        out[n][1] = sy - spanY;
        ++n;
    }
    if (nearTop) {
        out[n][0] = sx;
        out[n][1] = sy + spanY;
        ++n;
    }
    // 角上：x、y 同时贴界时补对角副本（如左下兵的对角副本 = 右上）。
    if (nearLeft && nearBottom) {
        out[n][0] = sx + spanX;
        out[n][1] = sy - spanY;
        ++n;
    }
    if (nearLeft && nearTop) {
        out[n][0] = sx + spanX;
        out[n][1] = sy + spanY;
        ++n;
    }
    if (nearRight && nearBottom) {
        out[n][0] = sx - spanX;
        out[n][1] = sy - spanY;
        ++n;
    }
    if (nearRight && nearTop) {
        out[n][0] = sx - spanX;
        out[n][1] = sy + spanY;
        ++n;
    }
    return n;
}

}  // namespace lw::render
