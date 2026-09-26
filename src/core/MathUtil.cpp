#include "core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace lw::math {

double distance(double x1, double y1, double x2, double y2) {
    return std::sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

double getAngle(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1, dy = y2 - y1;
    if (dy >= 0) {
        if (dx > 0) return std::atan2(dy, dx);
        if (dx == 0) return kPi / 2;
        if (dx < 0) return kPi - std::atan2(dy, -dx);
    }
    if (dy < 0) {
        if (dx > 0) return -std::atan2(-dy, dx);
        if (dx == 0) return -kPi / 2;
        if (dx < 0) return kPi + std::atan2(-dy, -dx);
    }
    return kPi / 2;
}

double getRandomAngle(Rng& rng) {
    // 原版 get_rand_angle 用 GetRand(14446)*π*2/14447（离散 14447 档）；现用连续均匀 [0, 2π)
    // 更现代（kRandAngleMax 保留作历史注记）。
    return rng.range(0.0, 2.0 * kPi);
}

unsigned mixColor(unsigned color1, unsigned color2, double rate) {
    // rate 越界/NaN 时通道值可能溢出并污染相邻通道 → 逐通道 clamp 到 [0,255]（L5）。
    const auto channel = [rate](unsigned c1, unsigned c2) {
        const double v = c1 * rate + c2 * (1.0 - rate);
        if (!(v > 0.0)) return 0u;  // NaN / <=0（NaN 比较恒 false，走此分支，避免 int 转换 UB）
        if (v > 255.0) return 255u;
        return static_cast<unsigned>(v);  // 与旧实现一致的截断语义
    };
    const unsigned r = channel((color1 >> 16) & 0xFF, (color2 >> 16) & 0xFF);
    const unsigned g = channel((color1 >> 8) & 0xFF, (color2 >> 8) & 0xFF);
    const unsigned b = channel(color1 & 0xFF, color2 & 0xFF);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// 连续版（唯一实现）：toScreenXf/Yf 保留小数；int 版 = f 版显式截断（2026-08 收敛）。
double toScreenXf(double x, double blockSize, int panelWidth) {
    return x * blockSize + panelWidth;
}

double toScreenYf(double y, double blockSize, double mapHeight) {
    return (mapHeight - y) * blockSize;
}

int toScreenX(double x, double blockSize, int panelWidth) {
    return static_cast<int>(toScreenXf(x, blockSize, panelWidth));
}

int toScreenY(double y, double blockSize, double mapHeight) {
    return static_cast<int>(toScreenYf(y, blockSize, mapHeight));
}

double pointDistanceFromSegment(double px, double py, double startX, double startY,
                                double angle, double length, double& closestX, double& closestY) {
    double theta = angle + kPi / 2;
    double alpha = getAngle(px, py, startX, startY);
    double endX = startX + std::cos(angle) * length;
    double endY = startY + std::sin(angle) * length;
    double beta = getAngle(px, py, endX, endY);
    while (theta < 0) theta += 2 * kPi;
    while (theta >= 2 * kPi) theta -= 2 * kPi;
    while (alpha < 0) alpha += 2 * kPi;
    while (alpha >= 2 * kPi) alpha -= 2 * kPi;
    while (beta < 0) beta += 2 * kPi;
    while (beta >= 2 * kPi) beta -= 2 * kPi;
    if (alpha > beta) std::swap(alpha, beta);
    double oppositeTheta = theta + kPi;
    if (oppositeTheta >= 2 * kPi) oppositeTheta -= 2 * kPi;
    double dist;
    if ((theta >= alpha && theta <= beta) || (oppositeTheta >= alpha && oppositeTheta <= beta)) {
        // 垂足在线段范围内。
        dist = std::fabs(std::sin(angle) * (px - startX) - std::cos(angle) * (py - startY));
        closestX = std::cos(angle) * std::cos(angle) * (px - startX)
                   + std::sin(angle) * std::cos(angle) * (py - startY) + startX;
        closestY = std::sin(angle) * std::cos(angle) * (px - startX)
                   + std::sin(angle) * std::sin(angle) * (py - startY) + startY;
    } else {
        double distStart = distance(px, py, startX, startY);
        double distEnd = distance(px, py, endX, endY);
        if (distStart < distEnd) {
            dist = distStart;
            closestX = startX;
            closestY = startY;
        } else {
            dist = distEnd;
            closestX = endX;
            closestY = endY;
        }
    }
    return dist;
}

double pointDistanceFromSegment(double px, double py, double startX, double startY,
                                double angle, double length) {
    double closestX, closestY;
    return pointDistanceFromSegment(px, py, startX, startY, angle, length, closestX, closestY);
}

}  // namespace lw::math
