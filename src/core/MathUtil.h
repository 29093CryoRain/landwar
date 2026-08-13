// MathUtil.h — 数学工具（原版 useful_tools.cpp 的 1:1 移植，见翻新计划 §2.12）。
#pragma once

#include "core/GameDefs.h"
#include "core/Random.h"

namespace lw::math {

double distance(double x1, double y1, double x2, double y2);

// 原版 get_angle：从 (x1,y1) 指向 (x2,y2) 的方向角，结果范围 (-π, π]。
double getAngle(double x1, double y1, double x2, double y2);

// 原版 get_rand_angle：离散化到 kRandAngleMax 档的 [0, 2π) 随机角。
double getRandomAngle(Rng& rng);

// 原版 mix_color：rate = color1 占比，逐通道 c1*rate + c2*(1-rate)。
unsigned mixColor(unsigned color1, unsigned color2, double rate);

// 原版 toscreenx / toscreeny（y 轴翻转：世界 y=0 在屏幕底）。
int toScreenX(double x, double blockSize, int panelWidth);
int toScreenY(double y, double blockSize, int mapHeight);

// 原版 find_next_xy：网格穿越，返回先撞到的边界方向并更新位置/余长。
// 返回值：0/2 = 水平边界(x1/x2)、1/3 = 垂直边界(y1/y2)；-2 = 走完本段；-3 = 撞角/两向相等（已反向）。
// 注意：撞角时会把 angle 反向（+=π + 微小偏置，2026-08 用户定夺；原版为随机角）并返回 -3。
int findNextXY(double& x, double& y, double& angle, double& remLength, Rng& rng);

// 原版 point_distance_from_segment：点到线段（起点 startX,startY + 方向 angle + 长度 length）距离。
double pointDistanceFromSegment(double px, double py, double startX, double startY,
                                double angle, double length, double& closestX, double& closestY);
double pointDistanceFromSegment(double px, double py, double startX, double startY,
                                double angle, double length);

// 屏幕坐标变换封装（渲染层使用）。
struct ScreenTransform {
    double blockSize = 15.0;
    int panelWidth = 600;
    int mapHeight = 95;

    int toX(double x) const { return toScreenX(x, blockSize, panelWidth); }
    int toY(double y) const { return toScreenY(y, blockSize, mapHeight); }
    // 连续（double）版：保留小数部分。整数世界坐标下与 int 版一致；城市中心等非整数坐标
    //（baseX+w/2）若走 int 版会在缩放前被抹掉 0.5 格 → 图标比地块中心恒偏左/上 0.5×zoom px
    // 且随 zoom 变化（2026-08-07 城市 1/2 级"略偏左"回归根因）。Camera 一律用 f 版。
    double toXf(double x) const { return x * blockSize + panelWidth; }
    double toYf(double y) const { return (mapHeight - y) * blockSize; }
};

}  // namespace lw::math
