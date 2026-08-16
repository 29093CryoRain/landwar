// Tiling.h — 密铺几何（P12 异种地图）：正方形/正六边形/正三角形 单元几何。
// 纯几何、无 SDL、无 RNG：世界单位 1 格 = 正方形边长（= blockSize px）。
// 边长按面积与正方形一致推导（开发计划 P12 §1）：
//   六边形（尖顶）a = √(2/(3√3))；三角形 b = 2·3^(-1/4)。
// 坐标约定（开发计划 P12 §2）：世界 y 向上增，y=0 = 地图底。
//   方：格 (x,y) 中心 (x+0.5, y+0.5)
//   六：行 r 中心 y = 1.5a·r + a/2（顶行顶点贴 worldHeight），列 c 中心 x = √3a·(c + (r mod 2)/2)
//   三：行 r 中心带 y = r·h + (1/3 或 2/3)h；P12 直边布局——所有行同列位，三角 j
//       左角 x = j·b/2（j ∈ [0,2N)），朝向随 (j+r) 奇偶翻转（行间无 b/2 平移），
//       左/右边缘贴 x=0 / x=N·b 直边，最右三角尖出 N·b + b/2 供环绕无缝。
// 格下标（cells_ 平铺数组下标，序列化/RNG 顺序固定）：
//   方：idx = y*cols + x
//   六：idx = r*cols + c
//   三：pair = r*cols + i（每行 cols 对 = 2·cols 格），正 idx = 2*pair，反 idx = 2*pair+1
// 邻格规范序（固定；先锋连占/穿越 RNG 相关顺序依赖它）：
//   方：0 下、1 上、2 左、3 右（沿用现 conquerCell 顺序）
//   六：0 右、1 右上、2 左上、3 左、4 左下、5 右下（边法向角 k·60°）
//   三：正：0 底、1 左斜、2 右斜；反：0 顶、1 右斜、2 左斜
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "core/GameDefs.h"

namespace lw {

struct TilingTable;

struct TilingGeom {
    TilingType type = TilingType::Square;
    int cols = 0;  // 列数（方 = width；六 = 每行列数；三 = 每行格对数）
    int rows = 0;  // 行数（六/三须为偶数——环绕闭合，见 P12 §2）
    // 半正/Laves 表驱动数据（惰性加载；Square/Hex/Tri 恒空）。
    mutable std::shared_ptr<const TilingTable> table_;

    // 面积相等边长（世界单位，固定推导值，不落 config）。
    static constexpr double kHexSide = 0.6204032394013997;    // √(2/(3√3))
    static constexpr double kTriSide = 1.5196713713031924;    // 2·3^(-1/4)
    static constexpr double kTriAlt = 1.3160740129524924;     // √3b/2 = 3^(1/4)
    static constexpr double kHexRowSpacing = 1.5 * kHexSide;  // 行距（y）
    static constexpr double kHexColSpacing = 1.7320508075688772 * kHexSide;  // √3a（列中心距）

    int cellCount() const;

    // 世界范围（世界单位；环绕周期 = (worldWidth, worldHeight)）。
    double worldWidth() const;

    double worldHeight() const;

    // 格下标 → 世界中心。
    void cellCenter(int index, double& wx, double& wy) const;

    // 格多边形顶点（世界坐标；六 6 / 三 3 顶点，逆时针）。返回顶点数（方返回 0）。
    // 渲染/预览/边界描线共用；maxVerts 至少 6（六边形）。
    int cellPolygon(int index, double* wx, double* wy, int maxVerts) const;

    // 第 k 条**邻接边**的端点（世界坐标；与 neighbor(idx,k) 同序——多边形顶点序 ≠ 邻接边序，
    // 边界描线/城市外廓必须以本方法取边，否则画错边）。返回 false = 非法（方/越界）。
    bool cellEdge(int index, int k, double& x0, double& y0, double& x1, double& y1) const;

    // 世界坐标 → 格下标（界外 -1）。
    int worldToCell(double wx, double wy) const;

    // 世界矩形（AABB）覆盖的行范围（含越界 ±1 保守外扩；供空间哈希/炸弹扫描）。
    // 输出直接 clamp 到 [0, rows)；矩形完全在图外时 r0 > r1（无覆盖）。
    void rowRange(double y0, double y1, int& r0, int& r1) const;
    // 世界 x 范围在给定行的列范围（含越界 ±1；clamp 到 [0, cols)）。
    void colRange(double x0, double x1, int r, int& c0, int& c1) const;

    // 边邻数（方 4 / 六 6 / 三 3）。表驱动类型可能逐格不同，请用 neighborCount(index)。
    int neighborCount() const;
    // 第 index 格的边邻数（方 4 / 六 6 / 三 3 / 表驱动 = 该格边数）。
    int neighborCount(int index) const;

    // 基础格数（表驱动 = 周期域内格数；方/六 = 1，三 = 2）。
    int baseCount() const;
    // 行列/基础格 → 格下标（方：b 忽略；六：b 忽略；三：b=0 正/1 反）。
    int cellIndexAt(int r, int c, int b) const;
    // 格下标 → 行列/基础格。
    void indexToRowCol(int index, int& r, int& c, int& b) const;

    // 第 k 个边邻的原始 (r, c)（不做界内检查；方 = (y, x) 语义，三 = (r, i) 语义）。
    void neighborRaw(int index, int k, int& r, int& c) const;

    // 第 k 个边邻下标（越界/图外返回 -1）。
    int neighbor(int index, int k) const;

    // 从格 index 内位置 (x,y) 沿 angle（rad）穿越到最近边。返回：
    //   0..neighborCount-1 = 穿过的边（进入格 = neighbor(index, 边)；越界由调用方按
    //   neighborRaw 判断环绕/反弹）；
    //   -2 = 剩余长度走完未撞边（位置已推进 remLength=0）；
    //   -1 = 无前向边命中（顶点命中 / 位置贴边 / 界外）——位置**不推进**，调用方应沿
    //   angle 方向 nudge kEps 后 worldToCell 重定位（顶点穿越确定性；3/6 格共点时
    //   取哪条边都会进错格，故直接穿顶点）。
    // 更新 (x,y)、remLength。RNG 0 次。
    int crossEdge(int index, double& x, double& y, double angle, double& remLength) const;

    // ---- 锚格朝向修复（2026-08-16）：表驱动密铺的形状表按"参考基础格"朝向编写 ----
    // 锚格为同边数但不同朝向的基础格时，形状偏移须旋转/镜像后才能 worldToCell 解析。
    // 基础格 b 相对"同边数第一个基础格"的变换（旋转角 rad + 镜像标志；非表驱动恒恒等）。
    bool baseCellTransform(int baseB, double& angleRad, bool& reflect) const;
    // 形状的参考基础格：anchorBaseMask 非 0 → 取最低置位（形状按该朝向族编写，
    // 如 Arch3464 L1/L2 仅限横平竖直正方形 b=2/8）；否则 → 该边数第一个基础格。
    int shapeReferenceBase(int anchorN, std::uint32_t anchorBaseMask) const;
    // 应用锚格朝向变换到形状偏移 (ox,oy)（运行时朝向、参考基础格 frame → 锚格 frame）。
    // 返回 false = 无表/参数非法（偏移保持不变）。
    bool applyAnchorOrientation(int index, int anchorN, std::uint32_t anchorBaseMask,
                                double& ox, double& oy) const;

private:
    void ensureTable() const;
};

// 表驱动密铺（半正/Laves）的周期域列/行选择：
// 用户宽先向上取整到 B 的倍数（且 ≤200 的 B 倍数），使 cols*rows*B = 用户长*有效宽
// （总格数）精确成立；再使实际视觉宽高比 (worldWidth*cols)/(worldHeight*rows)
// 尽量接近 用户长/有效宽。遍历 P=长*有效宽/B 的因子求最优 rows。
// 对非表驱动类型按原语义返回。
void chooseTableDomain(int tilingType, int userLength, int userWidth, int& cols, int& rows);

}  // namespace lw
