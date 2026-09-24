// Tiling.h — 密铺几何（P12 异种地图）：正方形/正六边形/正三角形 单元几何。
// 纯几何、无 SDL、无 RNG：世界单位 U 以正方形单格边长为 1（= blockSize px）。
// 其他密铺按平均单格面积与正方形相同归一化，因此其单格边长可能不是 1 U。
// 边长按面积与正方形一致推导（开发计划 P12 §1）：
//   六边形（尖顶）a = √(2/(3√3))；三角形 b = 2·3^(-1/4)。
// 坐标约定（开发计划 P12 §2）：世界 y 向上增，y=0 = 地图底。
// 所有密铺均由周期平移块描述：idx = (r*cols+c)*B+b，B 为块内基础格数。
// square 的 B=1，hex 的 B=2，tri 的 B=4；三种规则密铺的平移方向正交。
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
    TilingGeom() = default;
    TilingGeom(TilingType tiling, int columnCount, int rowCount)
        : type(tiling), cols(columnCount), rows(rowCount) {}

    TilingType type = TilingType::Square;
    int cols = 0;  // 周期块列数
    int rows = 0;  // 周期块行数
    // 周期块规格数据（惰性加载）。
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

    // ---- 世界坐标域（2026-08 斜周期：33336 系平行四边形周期域）----
    // 平行四边形域的首格中心可落于负坐标（W.y/H.x 剪切），且 AABB 原点随 cols/rows 变化。
    // 为使全体消费方（相机夹取 / 兵特效夹取 / 空间哈希）沿用 [0,worldWidth]x[0,worldHeight]，
    // TilingGeom 统一把几何**平移**到该域（cellCenter/cellPolygon 输出已含 worldMin 偏移）。
    // worldMinX/Y 即该平移量（域 AABB 下界）。
    double worldMinX() const;
    double worldMinY() const;
    // 世界 AABB 上界 = worldMin + worldWidth/Height。
    double worldMaxX() const { return worldMinX() + worldWidth(); }
    double worldMaxY() const { return worldMinY() + worldHeight(); }
    // 斜周期域（W.y 或 H.x 非 0）判定：此类地图几何经 worldMin 平移后仍为平行四边形，
    // 渲染/预览/生成须按"常规矩形"（格坐标）处理（见 MapPreview/MapGenerator）。
    bool hasSkewedPeriod() const;

    // 格下标 → 世界中心。
    void cellCenter(int index, double& wx, double& wy) const;

    // 格多边形顶点（世界坐标；方 4 / 六 6 / 三 3 顶点，逆时针）。返回顶点数。
    // 渲染/预览/边界描线共用；maxVerts 至少 6（六边形）。
    int cellPolygon(int index, double* wx, double* wy, int maxVerts) const;

    // 格多边形顶点在**常规矩形（格坐标）空间**（2026-08 斜周期"旋转矩阵"）。
    // 对斜周期域（W.y/H.x 剪切）：grid = R^{-1}{世界}（R = 格基 [W H]），把平行四边形
    // 地图"旋转"成矩形（格在下 [0,cols]x[0,rows]，形状被剪切但占据矩形域）。
    // 供预览/渲染把斜周期地图显示为常规矩形；非斜周期 = 世界坐标（无剪切）。
    int gridPolygon(int index, double* gx, double* gy, int maxVerts) const;

    // 格中心在格坐标空间（= gridPolygon 的离散中心；非斜周期 = 世界坐标）。
    void gridCenter(int index, double& gx, double& gy) const;

    // 格多边形到有限周期域四条边的最短垂直距离（世界单位）。周期域可旋转或剪切；
    // 返回 0 表示格接触/越过边界，非法格也返回 0。供地图生成的边缘海拔衰减使用。
    double cellBoundaryDistance(int index) const;
    // **格心**到有限周期域四条边的最短垂直距离（世界单位）。与 cellBoundaryDistance 同样处理
    // 斜周期；区别是只取格心一个点 ⇒ 随位置**连续**变化，不会在相邻大小格之间跳变
    //（地图生成的边缘衰减用它：衰减是海拔场的位置函数，不应依赖格的几何尺寸）。
    double centerBoundaryDistance(int index) const;

    // 第 k 条**邻接边**的端点（世界坐标；与 neighbor(idx,k) 同序——多边形顶点序 ≠ 邻接边序，
    // 边界描线/城市外廓必须以本方法取边，否则画错边）。返回 false = 非法（方/越界）。
    bool cellEdge(int index, int k, double& x0, double& y0, double& x1, double& y1) const;

    // 世界坐标 → 格下标（界外 -1）。规则密铺优先使用解析几何，其他密铺使用固定分区或
    // 周期细网格；边界/顶点/未命中时回退到精确半平面扫描。
    int worldToCell(double wx, double wy) const;

    // 世界矩形（AABB）覆盖的行范围（含越界 ±1 保守外扩；斜周期同时使用 x 范围收紧）。
    // 输出直接 clamp 到 [0, rows)；矩形完全在图外时 r0 > r1（无覆盖）。
    void rowRange(double x0, double y0, double x1, double y1, int& r0, int& r1) const;
    // 世界 x 范围在给定行的列范围（含越界 ±1；clamp 到 [0, cols)）。
    void colRange(double x0, double x1, int r, int& c0, int& c1) const;

    // 边邻数（方 4 / 六 6 / 三 3）。表驱动类型可能逐格不同，请用 neighborCount(index)。
    int neighborCount() const;
    // 第 index 格的边邻数（方 4 / 六 6 / 三 3 / 表驱动 = 该格边数）。
    int neighborCount(int index) const;

    // 基础格数（周期块内格数）。所有索引接口均按此值布局。
    int baseCount() const;
    // 周期块行列/基础格 → 格下标。
    int cellIndexAt(int r, int c, int b) const;
    // 格下标 → 行列/基础格。
    void indexToRowCol(int index, int& r, int& c, int& b) const;

    // 第 k 个边邻的原始 (r, c)（不做界内检查；方 = (y, x) 语义，三 = (r, i) 语义）。
    void neighborRaw(int index, int k, int& r, int& c) const;

    // 第 k 个边邻下标（越界/图外返回 -1）。
    int neighbor(int index, int k) const;

    // 点邻数（共享任一顶点；方 8 / 六 6 / 三 12 / 表驱动 = 该格顶点邻数）。
    // 数量按无限密铺的几何邻域返回，边界上的具体邻格由 pointNeighbor 返回 -1。
    int pointNeighborCount(int index) const;
    // 第 k 个点邻下标（越界/图外返回 -1，结果已去重）。
    int pointNeighbor(int index, int k) const;

    // 地块面积与内切圆圆心（世界坐标）。非表驱动密铺均为单位面积，内切圆圆心等于格中心。
    double cellArea(int index) const;
    void cellIncenter(int index, double& wx, double& wy) const;

    // 从格 index 内位置 (x,y) 沿 angle（rad）穿越到最近边。返回：
    //   0..neighborCount-1 = 穿过的边（进入格 = neighbor(index, 边)；越界由调用方按
    //   neighborRaw 判断环绕/反弹）；
    //   -2 = 剩余长度走完未撞边（位置已推进 remLength=0）；
    //   -1 = 无前向边命中（顶点命中 / 位置贴边 / 界外）——位置**不推进**，调用方应沿
    //   angle 方向 nudge kEps 后 worldToCell 重定位（顶点穿越确定性；3/6 格共点时
    //   取哪条边都会进错格，故直接穿顶点）。
    // 更新 (x,y)、remLength。RNG 0 次。
    int crossEdge(int index, double& x, double& y, double angle, double& remLength) const;

    // ================= 顶点/边拓扑（R1；《河流系统开发文档》§4）=================
    // 供河流系统使用的整数拓扑查询。构建期在 TilingTable 里按 kTableTol 把
    // (基础格, 顶点, dr, dc) 归并成等价类，运行期只做整数运算与键比较（零浮点比较）。
    // 键编码 = 1 + cell * 键宽 + idx（cell = 全图格下标，idx = 顶点/边序号）；0 恒为"无效"，
    // 故 (0,0) 不会与无效混淆。
    static constexpr int kMaxCellVerts = 16;  // 单格最大顶点数（当前最大 12-gon）
    static constexpr int kMaxCellEdges = 16;  // 单格最大边数（同上；与顶点共用键宽）

    // 格内顶点数（= cellPolygon 返回的顶点数）。
    int cellVertexCount(int index) const;
    // 顶点 v 的世界坐标（与 cellPolygon 第 v 个顶点逐字节一致）。
    void cellVertex(int index, int v, double& wx, double& wy) const;
    // 顶点 v 的**格坐标**（斜周期 = R^{-1}·世界，非斜 = 世界；与 gridPolygon 一致）。
    // 随机生成的梯度向量必须与本函数同帧，否则剪切会偏（§9.6）。
    void gridVertex(int index, int v, double& gx, double& gy) const;

    // 顶点规范键：同一几何顶点在全图唯一；取全部**图内**等价表示 (cell, vertex) 的字典序
    // 最小者，故与调用方用哪个表示无关。越界/图外返回 0。
    std::uint64_t vertexKey(int index, int v) const;
    // 键解码（顶点键；键无效/序号越界返回 false）。
    bool vertexFromKey(std::uint64_t key, int& index, int& v) const;

    // 顶点邻点 = 与该顶点共享一条边的全部顶点（枚举图内格的入射边，按规范键去重）。
    // 顺序确定（vertexLinks 链接序 × 前一/后一顶点）。顶点无效返回 0。
    int vertexNeighborCount(int index, int v) const;
    // 第 k 个邻点：outCell/outVert = 该邻点在**连接边所属格**上的表示（多边形顶点序）。
    // k 越界返回 -1，成功返回 k。
    int vertexNeighbor(int index, int v, int k, int& outCell, int& outVert) const;
    // 第 k 个邻点的规范顶点键；越界返回 0。
    std::uint64_t vertexNeighborKey(int index, int v, int k) const;
    // 第 k 个邻点的**连接边**（规范边表示）：outCell/outK 可直接喂给 cellEdge/edgeKey。
    // 越界返回 -1，成功返回 k。
    int vertexNeighborEdge(int index, int v, int k, int& outCell, int& outK) const;

    // 顶点在**本格内**关联的边（格内边序号 k，与 neighbor/cellEdge 同序）：恒 2 条。
    // which = 0 → v 与 vertexAdj(v)[0]（前一顶点）之间的边；which = 1 → 与后一顶点。
    int cellVertexEdgeCount(int index, int v) const;
    int cellVertexEdge(int index, int v, int which) const;
    // 格内边 (index,k) 的两个端点**多边形顶点序号**。
    bool cellEdgeVertices(int index, int k, int& vA, int& vB) const;

    // 连接顶点 (index,v) 与 (neighborCell,neighborVert) 的格内边序号 k（与 neighbor 同序，
    // 即 neighbor(index,k) == neighborCell）；无此边（该边不属于 index / 两者不相邻）返回 -1。
    // 两个顶点参数可以是同一几何顶点的**任一**表示（按规范键比对），无浮点比较。
    int edgeIndexBetweenVertices(int index, int v, int neighborCell, int neighborVert) const;

    // 边规范键：同一条几何边的两侧格给出同一键；地图边界边（对侧在图外）只保留本侧编码。
    // edgeKey 热路径 O(1)（用 TilingTable::edgeReverseK）；无效返回 0。
    std::uint64_t edgeKey(int index, int k) const;
    // 便捷版：扫邻居找 k，O(边数)；neighborIndex 不是 index 的边邻时返回 0。
    std::uint64_t edgeKeyBetween(int index, int neighborIndex) const;
    // 键解码（边键；键无效/序号越界返回 false）。
    bool edgeFromKey(std::uint64_t key, int& index, int& k) const;
    // 边端点世界坐标（与 cellEdge(解码出的 cell,k) 完全一致）。
    bool edgeEndpoints(std::uint64_t key, double& x0, double& y0, double& x1, double& y1) const;
    // 边的两侧格：outA 恒为规范键所属格，outB = 对侧（-1 = 地图边界边）；无效时两者 -1。
    void edgeCells(int index, int k, int& outA, int& outB) const;

    // ---- 地块双色分档（2026-08 异种地图开发思路「与双色渲染系统」）----
    // 按 格类型/朝向 分档渲染；方（单色）、六（单朝向）不分档：
    //   arch：按正多边形种类（边数）升序分档 → 档数 = 唯一边数 k；每格档 = 其边数排序序。
    //   laves：统计全部格的朝向（旋转+镜像），按角度排序；以朝向种类数 G 的最小质因子 p
    //          为循环 → 档数 = p；每格档 = 朝向排序序 mod p。
    //   三角（2026-08 用户定夺）：正/反两种朝向，同 laves 分档（朝向 2 → 最小质因子 2 循环
    //          → 2 档），每格档 = 格下标奇偶（偶 = 正 = 0，奇 = 反 = 1）。
    // 档数（0 = 不分档，方/六恒 0）。纯几何、RNG 0；供渲染层计算各档配色（factionTileColor t）。
    int tilePaletteSize() const;
    // 格 index 的档位（[0, tilePaletteSize()-1]；tilePaletteSize()<=1 → 0）。非表驱动恒 0。
    int tileColorIndex(int index) const;

    // ---- 城市形状锚格朝向（2026-08-26 方案A）：形状表已烘焙为世界帧，锚格朝向由数据
    //（baseGroups/anchorBases）自然保证，运行时不需再旋转/镜像（applyAnchorOrientation 已移除）。
    // 方/六/三与锚格朝向无关（恒等）。

private:
    void ensureTable() const;
};

// 所有密铺的周期域列/行选择（2026-08 依 .docs/地图尺寸比例映射.md 两阶段算法）：
// 硬约束：① a*b = B*cols*rows（守恒）② cols 为 s 倍数、rows 为 t 倍数（s*t=B 每块格数）
//          ③ cols ∝ a、rows ∝ b（正比例 → 调长只改cols、调宽只改rows，完全单调方向一致）。
// 优化：④ (cols*u)/(rows*v) ≈ a/b（u=wx/s、v=wy/t 每格世界比例，算法一离线求 p/q≈√(v/u)）。
// 算法一（离线，硬编码）：每密铺求互质 p,q + 输入限制 Ra,Rb（见 Tiling.cpp 的 tableDomainParams）。
// 算法二（在线）：a'=ceil(a/Ra)*Ra、b'=ceil(b/Rb)*Rb；c=p·a'/q、d=q·b'/p；cols=c/s、rows=d/t。
// square 为恒等映射；hex/tri 与表驱动密铺统一按周期域比例映射。
void chooseTableDomain(int tilingType, int userLength, int userWidth, int& cols, int& rows);

// 密铺的"输入限制"：长须为 Ra 倍数、宽须为 Rb 倍数（算法一硬编码，见 Tiling.cpp）。
// 菜单据此设置步进/夹取，使输入天然合规（长*宽=总格数精确，无需微调）。
// square 无需限制，返回 false；hex/tri 及表驱动密铺返回 true。
bool tableInputRestriction(int tilingType, int& ra, int& rb);

}  // namespace lw
