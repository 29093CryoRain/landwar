// Camera.h — 屏幕↔世界坐标变换（缩放/平移）（翻新计划 Phase 8，§3.1 render/）。
// 所有渲染器统一经 Camera 把世界坐标换算到屏幕像素，取代原先直接使用 ScreenTransform。
// 默认 zoom=1、pan=0 → 与 Phase 7 完全一致（零视觉回归）；滚轮缩放锚定光标、右键拖拽平移
// （2026-08-03 交互调整：拖动屏幕改右键）。
// 只影响渲染视图，不触碰模拟状态 → 确定性/回放成立。
#pragma once

#include <SDL.h>

#include <algorithm>
#include <cmath>

#include "core/MathUtil.h"

namespace lw::render {

class Camera {
public:
    // 默认：逻辑视口 2175×1425、整张地图（配置 init 后立即 configure 覆盖）。
    Camera() = default;

    // 以屏幕变换 + 逻辑视口尺寸 + 地图尺寸（世界单位）初始化。
    // P12：mapWidth/mapHeight 为世界范围（六/三角非整数；方 = 格数），环绕 span/剔除用。
    void configure(const math::ScreenTransform& tf, int windowW, int windowH, double mapWidth,
                   double mapHeight) {
        tf_ = tf;
        windowW_ = windowW;
        windowH_ = windowH;
        mapW_ = mapWidth;
        mapH_ = mapHeight;
        clampPan();
    }

    // ---- 世界坐标（U）→ 屏幕像素（逻辑坐标空间）----
    // 用连续变换（toXf/toYf）：整数世界坐标与旧 int 版一致，但非整数（城市中心等）不被
    // 抹掉小数 → 消除"图标偏左/上随 zoom 放大"（2026-08-07 城市 1/2 级偏左回归）。
    double toScreenX(double worldX) const { return tf_.toXf(worldX) * zoom_ + panX_; }
    double toScreenY(double worldY) const { return tf_.toYf(worldY) * zoom_ + panY_; }
    int toScreenXi(double worldX) const {
        return static_cast<int>(std::lround(toScreenX(worldX)));
    }
    int toScreenYi(double worldY) const {
        return static_cast<int>(std::lround(toScreenY(worldY)));
    }

    // 无缝隙格矩形：相邻格共享边界取整，缩放/平移时网格不留缝。
    // 注意屏幕 y 随世界 j 增大而【减小】（y 翻转），故用 min/abs 归一（高度恒正）。
    SDL_Rect cellRect(int i, int j) const {
        const int x0 = toScreenXi(static_cast<double>(i));
        const int y0 = toScreenYi(static_cast<double>(j));
        const int x1 = toScreenXi(static_cast<double>(i + 1));
        const int y1 = toScreenYi(static_cast<double>(j + 1));
        return SDL_Rect{std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
    }

    // ---- 屏幕像素 → 世界坐标（反向；点选/锚点）----
    double toWorldX(double screenX) const {
        return ((screenX - panX_) / zoom_ - tf_.panelWidth) / tf_.blockSize;
    }
    double toWorldY(double screenY) const {
        return tf_.mapHeight - (screenY - panY_) / zoom_ / tf_.blockSize;
    }

    // 可见世界坐标范围（剔除用）。屏幕 y=0 = 顶 = 世界 y=mapHeight。
    double viewWorldX0() const { return toWorldX(0.0); }
    double viewWorldX1() const { return toWorldX(windowW_); }
    double viewWorldY0() const { return toWorldY(windowH_); }  // 屏幕底（世界 y 小）
    double viewWorldY1() const { return toWorldY(0.0); }       // 屏幕顶（世界 y 大）

    // 每世界单位 U 的逻辑屏幕像素 = blockSize*zoom（特效半径/光束高度等世界几何用）。
    double cellPx() const { return tf_.blockSize * zoom_; }

    // 以屏幕点 (sx,sy) 为锚缩放：锚点下的世界坐标保持不动。factor>1 放大。
    // pan 用连续变换（toXf/toYf）：锚点世界坐标可能非整数，int 版会引入 ≤0.5px 锚点漂移。
    void zoomAt(double sx, double sy, double factor) {
        const double wx = toWorldX(sx);
        const double wy = toWorldY(sy);
        zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
        panX_ = sx - tf_.toXf(wx) * zoom_;
        panY_ = sy - tf_.toYf(wy) * zoom_;
        clampPan();
    }
    void pan(double dx, double dy) {
        panX_ += dx;
        panY_ += dy;
        clampPan();
    }
    void reset() {
        zoom_ = 1.0;
        panX_ = 0.0;
        panY_ = 0.0;
    }

    double zoom() const { return zoom_; }
    double panX() const { return panX_; }
    double panY() const { return panY_; }
    int windowWidth() const { return windowW_; }
    int windowHeight() const { return windowH_; }
    // P10：地图世界范围（渲染器/点选算环绕副本的屏幕跨度用；世界单位）。
    double mapWidth() const { return mapW_; }
    double mapHeight() const { return mapH_; }

    static constexpr double kMinZoom = 0.25;
    static constexpr double kMaxZoom = 4.0;

private:
    // 平移夹取：保证地图至少有一部分留在视口内；地图小于视口时居中。
    void clampPan() {
        const double mapLeft = tf_.toXf(0.0);             // = panelWidth
        const double mapRight = tf_.toXf(mapW_);
        const double mapBottom = tf_.toYf(0.0);           // = mapH*blockSize
        // X：视口与地图需有重叠 → 地图右缘 ≥ 0 且地图左缘 ≤ windowW。
        const double minPanX = -mapRight * zoom_;
        const double maxPanX = windowW_ - mapLeft * zoom_;
        if (minPanX > maxPanX) {
            panX_ = (windowW_ - (mapLeft + mapRight) * zoom_) / 2.0;  // 地图比视口窄 → 居中
        } else {
            panX_ = std::clamp(panX_, minPanX, maxPanX);
        }
        // Y：地图底缘 ≥ 0 且地图顶缘 ≤ windowH（屏幕顶 y=0 = 世界顶）。
        const double minPanY = -mapBottom * zoom_;
        const double maxPanY = windowH_;
        if (minPanY > maxPanY) {
            panY_ = (windowH_ - mapBottom * zoom_) / 2.0;
        } else {
            panY_ = std::clamp(panY_, minPanY, maxPanY);
        }
    }

    math::ScreenTransform tf_;
    int windowW_ = 2175;
    int windowH_ = 1425;
    double mapW_ = 105.0;   // P12：世界宽（六/三角非整数；方 = 列数）
    double mapH_ = 95.0;    // P12：世界高
    double zoom_ = 1.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
};

}  // namespace lw::render
