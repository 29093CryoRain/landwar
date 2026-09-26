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
// 2026-08 收敛：f 版（连续 double）为唯一实现，int 版 = f 版显式截断（语义与旧版逐位一致）。
// 规则：新代码一律用 f 版（ScreenTransform::toXf / Camera::toScreenX），保留小数；
// 绘制取整走 Camera::toScreenXi/Yi（lround）。int 版仅供历史逐像素对齐路径。
// P12：mapHeight 为世界高度（单位 U；六/三角非整数，方形恰为行数）。
double toScreenXf(double x, double blockSize, int panelWidth);
double toScreenYf(double y, double blockSize, double mapHeight);
int toScreenX(double x, double blockSize, int panelWidth);
int toScreenY(double y, double blockSize, double mapHeight);

// 原版 point_distance_from_segment：点到线段（起点 startX,startY + 方向 angle + 长度 length）距离。
double pointDistanceFromSegment(double px, double py, double startX, double startY,
                                double angle, double length, double& closestX, double& closestY);
double pointDistanceFromSegment(double px, double py, double startX, double startY,
                                double angle, double length);

// 屏幕坐标变换封装（渲染层使用）。
struct ScreenTransform {
    double blockSize = 15.0;
    int panelWidth = 600;
    double mapHeight = 95.0;  // P12：世界高度（六/三角非整数；方 = 行数）

    // 连续版（唯一实现）：保留小数。整数世界坐标下与 int 版一致；城市中心等非整数坐标
    //（baseX+w/2）若走 int 版会在缩放前被抹掉 0.5 格 → 图标比地块中心恒偏左/上 0.5×zoom px
    // 且随 zoom 变化（2026-08-07 城市 1/2 级"略偏左"回归根因）。Camera 一律用 f 版。
    double toXf(double x) const { return toScreenXf(x, blockSize, panelWidth); }
    double toYf(double y) const { return toScreenYf(y, blockSize, mapHeight); }
    // int 版（历史逐像素路径；截断语义与旧版一致）。新代码用 f 版 + 显式取整。
    int toX(double x) const { return toScreenX(x, blockSize, panelWidth); }
    int toY(double y) const { return toScreenY(y, blockSize, mapHeight); }
    // 反向变换（屏幕逻辑像素 → 世界 U）：正向 toXf/toYf 的显式反演。
    // Camera 反变换统一委托这两个函数，避免手写展开与正向变换各用一份字段（2026-09 修复隐式耦合）。
    double toWorldXf(double sx) const { return (sx - panelWidth) / blockSize; }
    double toWorldYf(double sy) const { return mapHeight - sy / blockSize; }
};

}  // namespace lw::math
