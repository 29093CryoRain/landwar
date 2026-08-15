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
    int r = static_cast<int>(((color1 >> 16) & 0xFF) * rate + ((color2 >> 16) & 0xFF) * (1 - rate));
    int g = static_cast<int>(((color1 >> 8) & 0xFF) * rate + ((color2 >> 8) & 0xFF) * (1 - rate));
    int b = static_cast<int>((color1 & 0xFF) * rate + (color2 & 0xFF) * (1 - rate));
    return 0xFF000000u | (static_cast<unsigned>(r) << 16) | (static_cast<unsigned>(g) << 8)
           | static_cast<unsigned>(b);
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

// rng 保留在签名中（历史 API：Phase 9 起每 tick 抖动已取消，调用方仍传）；
// 新代码请勿在 findNextXY 内消耗 RNG（确定性契约：本函数不掷骰）。
int findNextXY(double& x, double& y, double& angle, double& remLength, [[maybe_unused]] Rng& rng) {
    int x1 = static_cast<int>(x);
    int y1 = static_cast<int>(y);
    if (std::fabs(x1 - x) < kEps) x1--;
    if (std::fabs(y1 - y) < kEps) y1--;
    int x2 = static_cast<int>(x) + 1;
    int y2 = static_cast<int>(y) + 1;
    int hitX = -1, hitY = -1, code = -1;
    double minDistX = 9961, minDistY = 9961, minDist = 9961;
    double distToX1 = (std::fabs(std::cos(angle)) < kEps ? -9961 : (x1 - x) / std::cos(angle));
    double distToY1 = (std::fabs(std::sin(angle)) < kEps ? -9961 : (y1 - y) / std::sin(angle));
    double distToX2 = (std::fabs(std::cos(angle)) < kEps ? -9961 : (x2 - x) / std::cos(angle));
    double distToY2 = (std::fabs(std::sin(angle)) < kEps ? -9961 : (y2 - y) / std::sin(angle));
    if (distToX1 > 0 && minDistX > distToX1) {
        minDistX = distToX1;
        hitX = 0;
    }
    if (distToY1 > 0 && minDistY > distToY1) {
        minDistY = distToY1;
        hitY = 0;
    }
    if (distToX2 > 0 && minDistX > distToX2) {
        minDistX = distToX2;
        hitX = 1;
    }
    if (distToY2 > 0 && minDistY > distToY2) {
        minDistY = distToY2;
        hitY = 1;
    }
    if (std::fabs(minDistX - minDistY) < kEps) {
        // 撞角 / 两向边界距离相等：反向避开角落，返回 -3（2026-08 用户定夺；原版为随机转向 + 返回 -1）。
        // 加微小偏置（4×kEps）打破对称：格心 + 对角方向下，恰好 180° 反向会重复撞角 → 卡死。
        angle += kPi + 4.0 * kEps;
        return -3;
    }
    if (minDistX < minDistY) {
        code = hitX * 2;
        minDist = minDistX;
    } else {
        code = hitY * 2 + 1;
        minDist = minDistY;
    }
    if (minDist > remLength) {
        // 本段剩余长度不足以撞到边界：走完即止。
        x += remLength * std::cos(angle);
        y += remLength * std::sin(angle);
        remLength = 0;
        return -2;
    }
    if (code == -1) {
        remLength = 0;
        return -1;
    }
    if (code == 0)
        x = x1;
    else if (code == 2)
        x = x2;
    else
        x += minDist * std::cos(angle);
    if (code == 1)
        y = y1;
    else if (code == 3)
        y = y2;
    else
        y += minDist * std::sin(angle);
    remLength -= minDist;
    return code;
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
