#!/usr/bin/env python3
"""derive_city_shapes.py — 为扭棱类四密铺 (arch/laves 3.3.3.3.6、3.3.4.3.4) 推导城市形状表。

工作流：
  1. 读取当前 (轴对齐) spec (data/tiling_specs_arch.json / tiling_specs_laves.json)。
  2. 复刻 C++ 邻接构建 (Tiling.cpp loadTable)。
  3. 用"多边形顶点等距 + 邻格校验"求每基础格相对参考格的锚朝向变换 (旋转角 + 镜像)。
  4. 按 .docs/异种地图开发思路.txt 的"可能出现的城市"语义 + 方向规范枚举形状格。
  5. 输出 Config.cpp 片段：shape 偏移 = 目标格中心 − 锚格中心 (参考帧)，再按 Map.cpp 的
     运行时变换 (ox,oy)=(-dy,dx) 反存为 (dy, -dx)。
"""
import json
import math
import sys
from collections import defaultdict

TOL = 1e-6

# ---------------------------------------------------------------------------
# spec 载入与邻接 (与 C++ 一致)
# ---------------------------------------------------------------------------
def load_spec(name, file):
    d = json.load(open(file, encoding="utf-8"))
    j = d[name]
    return {
        "wx": j["W"][0], "wy": j["W"][1] if len(j["W"]) > 1 else 0.0,
        "hx": j["H"][0], "hy": j["H"][1],
        "cells": j["cells"],
    }

def same(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 <= TOL * TOL

def build_edges(tab):
    B = len(tab["cells"])
    edges = [[] for _ in range(B)]
    for b in range(B):
        cell = tab["cells"][b]
        n = cell["n"]
        for k in range(n):
            A = cell["v"][k]; Bv = cell["v"][(k + 1) % n]
            found = False
            for dr in range(-2, 3):
                for dc in range(-2, 3):
                    ox = dc * tab["wx"] + dr * tab["hx"]
                    oy = dc * tab["wy"] + dr * tab["hy"]
                    for nb in range(B):
                        ocell = tab["cells"][nb]
                        for kk in range(ocell["n"]):
                            n0 = (ocell["v"][kk][0] + ox, ocell["v"][kk][1] + oy)
                            n1 = (ocell["v"][(kk + 1) % ocell["n"]][0] + ox, ocell["v"][(kk + 1) % ocell["n"]][1] + oy)
                            if same(n0, Bv) and same(n1, A):
                                edges[b].append((nb, dr, dc))
                                found = True
                                break
                        if found:
                            break
                    if found:
                        break
                if found:
                    break
            if not found:
                edges[b].append(None)
    return edges

def center(tab, b, r=0, c=0):
    cell = tab["cells"][b]
    return (cell["cx"] + c * tab["wx"] + r * tab["hx"],
            cell["cy"] + c * tab["wy"] + r * tab["hy"])

def neighbor_center(tab, edges, b, k):
    e = edges[b][k]
    if e is None:
        return None
    nb, dr, dc = e
    return center(tab, nb, dr, dc)

# ---------------------------------------------------------------------------
# 锚朝向变换：多边形顶点等距 + 邻格校验
# ---------------------------------------------------------------------------
def same_loose(a, b, tol=2e-4):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 <= tol * tol

def find_isometry(tab, edges, refb, b):
    """返回把 refb 多边形映到 b 多边形 (含平移) 的 (angle, reflect)，或 None。

    angle/reflect 语义与 C++ baseCellTransform 一致：T(p) = R(angle)∘F(p − refc) + bc，
    F = 绕 x 轴镜像 (y → −y)。
    """
    rv = tab["cells"][refb]["v"]
    bv = tab["cells"][b]["v"]
    n = len(rv)
    if len(bv) != n:
        return None
    rc = center(tab, refb)
    bc = center(tab, b)

    def mapped(p, ang, reflect):
        dx, dy = p[0] - rc[0], p[1] - rc[1]
        if reflect:
            dy = -dy
        return (bc[0] + dx * math.cos(ang) - dy * math.sin(ang),
                bc[1] + dx * math.sin(ang) + dy * math.cos(ang))

    cands = []
    # 完整候选：镜像与否 × 顶点对应起始 i × 绕向 s。T = R(ang)∘F^reflect，
    # 顶点对应 v_k → bv[σ(k)]，σ = (i + s·k) mod n（reflect=0 保向）或 (i − s·k) mod n（reflect=1 反向）。
    for reflect in (0, 1):
        for i in range(n):
            for s in (1, -1):
                def sigma(k):
                    return (i + s * k) % n if not reflect else (i - s * k) % n
                # 前两点确定角度
                p0 = rv[0]
                q0 = bv[sigma(0)]
                p1 = rv[1]
                q1 = bv[sigma(1)]
                v = (p1[0] - p0[0], p1[1] - p0[1])
                w = (q1[0] - q0[0], q1[1] - q0[1])
                # 反射情形需先镜像参考向量：R(ang)·F(v) = w
                fv = (v[0], -v[1]) if reflect else v
                lv = math.hypot(*fv)
                lw = math.hypot(*w)
                if abs(lv - lw) > 1e-6 or lv < 1e-9:
                    continue
                ang = math.atan2(w[1], w[0]) - math.atan2(fv[1], fv[0])
                ok = True
                for k in range(2, n):
                    pp = rv[k]
                    x, y = mapped(pp, ang, bool(reflect))
                    if not same_loose((x, y), bv[sigma(k)]):
                        ok = False
                        break
                if ok:
                    cands.append((ang, bool(reflect)))
    if not cands:
        return None
    # 邻格校验：找出把 ref 邻格中心集映射到 b 邻格中心集 (作为集合) 的候选。
    rnc = [neighbor_center(tab, edges, refb, k) for k in range(len(edges[refb]))]
    rnc = [p for p in rnc if p is not None]
    bnc = [neighbor_center(tab, edges, b, k) for k in range(len(edges[b]))]
    bnc = [p for p in bnc if p is not None]
    for ang, cand_reflect in cands:
        hit = 0
        for p in rnc:
            m = mapped(p, ang, cand_reflect)
            for q in bnc:
                if same_loose(m, q, tol=5e-4):
                    hit += 1
                    break
        if hit == len(rnc) and len(rnc) == len(bnc):
            return (ang, cand_reflect)
    # 邻格组成不同（如 arch_33336 三角：参考格邻 2 三角+1 六边，部分三角邻 3 三角）：
    # 无法用邻格校验 → 取第一个多边形一致的候选（候选已含镜像选项）。
    if cands:
        return cands[0]
    return None

def compute_anchor_transforms(tab, edges):
    B = len(tab["cells"])
    first = {}
    for b in range(B):
        n = tab["cells"][b]["n"]
        if n not in first:
            first[n] = b
    tr = {}
    for b in range(B):
        n = tab["cells"][b]["n"]
        ref = first[n]
        if ref == b:
            tr[b] = (0.0, False)
            continue
        r = find_isometry(tab, edges, ref, b)
        tr[b] = r if r is not None else (0.0, False)
    return tr, first

# ---------------------------------------------------------------------------
# 形状枚举辅助
# ---------------------------------------------------------------------------
def wrap_center(tab, p):
    """把世界点折回基础域 (b=0 帧附近) 得到 (b, r, c)，用于跨周期去重。"""
    best = None
    bestd = 1e18
    B = len(tab["cells"])
    for b in range(B):
        for r in range(-3, 4):
            for c in range(-3, 4):
                cx, cy = center(tab, b, r, c)
                d2 = (p[0] - cx) ** 2 + (p[1] - cy) ** 2
                if d2 < bestd:
                    bestd = d2
                    best = (b, r, c)
    return best

def vertex_bucket(tab, p, radius=4):
    """归一化顶点键 → 基础格实例 (b,r,c) 列表 (共享该物理顶点的全部格)。"""
    vm = defaultdict(list)
    B = len(tab["cells"])
    for b in range(B):
        for r in range(-radius, radius + 1):
            for c in range(-radius, radius + 1):
                ox = c * tab["wx"] + r * tab["hx"]
                oy = c * tab["wy"] + r * tab["hy"]
                for v in tab["cells"][b]["v"]:
                    key = (round((v[0] + ox) * 1e4), round((v[1] + oy) * 1e4))
                    vm[key].append((b, r, c))
    return vm.get((round(p[0] * 1e4), round(p[1] * 1e4)), [])

def edge_len(tab, b, k):
    cell = tab["cells"][b]
    p = cell["v"][k]; q = cell["v"][(k + 1) % cell["n"]]
    return math.hypot(q[0] - p[0], q[1] - p[1])

def cell_key(b, r, c):
    return (b, r, c)

def uniq(ccs):
    seen = set()
    out = []
    for cc in ccs:
        k = cell_key(*cc)
        if k in seen:
            continue
        seen.add(k)
        out.append(cc)
    return out

def fmt_shape(offs, anchorN, mask=0, prec=9):
    """offs = 参考帧偏移 (Δx, Δy)；输出 Config 存储 (dx,dy) = (Δy, −Δx)。"""
    cells = []
    for rx, ry in offs:
        cells.append("{%.*f, %.*f, 0}" % (prec, round(ry, prec), prec, round(-rx, prec)))
    mask_str = "" if mask == 0 else ", 0x%x" % mask
    return "shp(%d, {%s}%s)" % (anchorN, ", ".join(cells), mask_str)

if __name__ == "__main__":
    # 简易自检：锚朝向变换。
    for name, file in [("arch_33336", "data/tiling_specs_arch.json"),
                       ("arch_33434", "data/tiling_specs_arch.json"),
                       ("laves_33336", "data/tiling_specs_laves.json"),
                       ("laves_33434", "data/tiling_specs_laves.json"),
                       ("laves_4612", "data/tiling_specs_laves.json")]:
        tab = load_spec(name, file)
        edges = build_edges(tab)
        tr, first = compute_anchor_transforms(tab, edges)
        B = len(tab["cells"])
        nfail = sum(1 for b in range(B) if tr[b] is None)
        print("%s B=%d anchor transforms: %d missing" % (name, B, nfail))
        for b in range(B):
            if tr[b] is None:
                print("   FAIL base", b, "n", tab["cells"][b]["n"], "ref", first[tab["cells"][b]["n"]])
