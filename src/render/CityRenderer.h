// CityRenderer.h — P13 城市渲染（render/CityRenderer，改造 MapRenderer 城市绘制）。
// 两部分绘制（顺序即层级，在 MapRenderer 陆地/山贴图之后）：
//   1. 等级塔图标（data/tower/tower<N>.png，9 级高塔贴图，文件名即等级；画于城市中心；
//      尺寸按基建地块框自适应（2026-08-07 反馈）：框 = 形状 w×h 个格子，保留源纵横比
//      下能放进框的最大宽 fitW = min(w×cellPx, h×cellPx/A)，图标宽 = max(minIconSizePx,
//      round(fitW×iconScale[level-1]))，图标高 = round(宽×A)——长/宽都不超出框且略小
//      （iconScale<1），避免贴图过大/紧贴框边缘；低缩放取最小尺寸不继续缩小；
//      按势力色 tint，同玩家指示管线）；
//   2. 高缩放细线（屏上格 > render.city.lineMinCellPx）围基建地块区域（外廓矩形），
//      细线颜色 = 势力色 × render.city.lineDarken（与图标同势力色处理但加深）。
//   （2026-08-07 用户反馈：去除 data/city.png 基建格贴图——城市只由塔图标 + 细线表示。）
// 图标中心 = 基建地块几何中心（2026-08-07：移除 offsetY 错开偏移）。
// 渲染常数全外置 render.city 段（lineMinCellPx/minIconSizePx/lineThickness/lineDarken/
// iconScale[]）。
//
// **纯几何（compute）与 SDL 绘制分离** → 可 mock 相机/缩放单测（无需 SDL）：
// compute() 返回绘制命令（细线/图标），draw() 内部调用 compute 并落纹理。
// 源纵横比由 bake() 记录（compute 只读，不碰纹理）——图标尺寸可在纯几何中算出。
// 纯渲染层，不碰模拟；确定性不受影响。
#pragma once

#include <array>
#include <vector>

#include <SDL.h>

#include "core/Config.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "render/TintCache.h"
#include "world/Map.h"

namespace lw::render {

class CityRenderer {
public:
    CityRenderer(SDL_Renderer* ren, const Camera& cam) : ren_(ren), cam_(cam) {}
    // 纯几何单测构造：无 SDL 渲染器（ren_=nullptr）。bake/draw 在 null 下安全跳过，
    // compute 完全可用（不碰渲染器）。
    explicit CityRenderer(const Camera& cam) : ren_(nullptr), cam_(cam) {}

    // 烘焙 9 级塔贴图 + 首都图标（factionColors 下标即势力 id，含中立 0 共 9 项），并留存势力色
    // 供细线加深用 + 记录各塔/首都源图纵横比（图标框适配用）。可重复调用（F5 重烘焙）。
    // iconDarken = render.city.iconDarken（图标色 = 势力色 × 该值；2026-08-07 城市图标调暗）。
    // P15：另载 data/tower/capital.png（白色前景透明背景，与其他城市贴图同类）→ capital_。
    void bake(const std::vector<std::array<int, 3>>& factionColors, double iconDarken = 1.0);
    void reloadColors(const std::vector<std::array<int, 3>>& factionColors, double iconDarken = 1.0) {
        bake(factionColors, iconDarken);
    }
    // 纯几何单测用：直接指定等级塔源图宽高（模拟 bake 记录的贴图尺寸，推导纵横比）。
    // 未指定 → 该等级按 1.0（方形）处理。
    void setTowerSourceSize(int level, int srcW, int srcH);
    // 纯几何单测用：指定首都图标源图宽高（模拟 bake 记录，推导纵横比；未指定 → 1.0 方形）。
    void setCapitalSourceSize(int srcW, int srcH);

    // ---- 纯几何（单测用，无需 SDL）----
    // 基建地块外廓矩形（屏幕像素；仅高缩放档生成）。颜色 = 该城归属势力的加深色。
    struct Outline {
        int x, y, w, h;
        int colorIndex;   // 势力色下标（= City.ownerId）
    };
    // 图标（城市中心；尺寸按基建地块框适配，dstW 已含 minIconSizePx 下限，dstH 已按源纵横比
    // 算好——长/宽均 ≤ 框尺寸 × iconScale）。等级塔用 towers_[level-1]；首都/候补用 capital_。
    struct Icon {
        int level;        // 等级 1..9（框适配缩放表下标 = 等级-1）
        int cx, cy;       // 图标中心（屏幕像素，= 城市基建块几何中心）
        int dstW;         // 图标宽（≥ minIconSizePx）
        int dstH;         // 图标高（≥ 1，= round(dstW × 源高/源宽)）
        int colorIndex;   // 势力色下标（= City.ownerId）
    };
    // capitalStatus[c.id]：0=普通城市、1=正式首都、2=候补指定新都（虚化首都图标）。
    // 空向量 = 全普通（向后兼容，单测可省）。
    struct Frame {
        std::vector<Outline> outlines;        // 普通城市细线（render.city.lineThickness）
        std::vector<Outline> capitalOutlines; // 正式首都细线（render.capital.lineThickness 更粗）
        std::vector<Icon> icons;              // 普通城市等级塔图标
        std::vector<Icon> capitalIcons;       // 正式首都图标（data/tower/capital.png，全不透明）
        std::vector<Icon> designatedIcons;    // 候补指定新都（虚化首都图标，designatedAlpha）
    };
    // 计算当前相机/渲染常数下的绘制命令。rc = render 段（city + capital 渲染常数）。
    // 高缩放判定在内部（cam.cellPx() > rc.city.lineMinCellPx）。视野剔除：地块矩形与视口
    // 不相交 → 整城跳过。capitalStatus 见 Frame 注（空 = 全普通）。
    Frame compute(const Map& map, const Config::Render& rc,
                  const std::vector<int>& capitalStatus = {}) const;

    // 实际绘制（需要纹理；内部调 compute）。rc = render 段（city + capital）。
    void draw(const Map& map, const Config::Render& rc, const std::vector<int>& capitalStatus = {});

private:
    SDL_Renderer* ren_;
    const Camera& cam_;
    std::vector<std::array<int, 3>> factionColors_;  // 势力色（含中立 0；细线加深用）
    std::array<double, 9> srcAspect_{};   // 各等级塔源图纵横比 高/宽（下标 = 等级-1；0 → 按 1.0）
    double capitalAspect_ = 0.0;          // 首都图标源图纵横比 高/宽（0 → 按 1.0）
    std::array<TintCache, 9> towers_;      // 9 级塔贴图（data/tower/tower<N>.png，下标 = 等级-1）
    TintCache capital_;                    // 首都图标贴图（data/tower/capital.png，P15）
};

}  // namespace lw::render
