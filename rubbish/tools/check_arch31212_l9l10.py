#!/usr/bin/env python3
"""验证 Arch31212 L9/L10 形状在正/反三角形锚上的形态。

形状表 Config 存"旧中间朝向"偏移；运行时 (dx,dy)→(-dy,dx) 再 applyAnchorOrientation。
这里模拟：对每个三角形基础格 b（锚），把形状偏移变换到该格朝向，解析出格子集合，
对比 b=0（正三角）与 b=3（反三角）的集合是否不同（正反两形态）。
"""
import json
import math
from collections import defaultdict

TOL = 1e-6

d = json.load(open('data/tiling_specs_arch.json', encoding='utf-8'))
j = d['arch_31212']
old_wx, old_hy = j['W'][0], j['H'][1]
cells = []
for c in j['cells']:
    nc = old_hy - c['cy']
    ny = c['cx']
    v = [[old_hy - p[1], p[0]] for p in c['v']]
    cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
tab = {'wx': old_hy, 'hx': 0.0, 'hy': old_wx}
B = len(cells)

# 邻接
edges = []
for b in range(B):
    be = []
    cell = cells[b]
    for k in range(cell['n']):
        A = cell['v'][k]
        Bv = cell['v'][(k + 1) % cell['n']]
        found = False
        for dr in range(-2, 3):
            for dc in range(-2, 3):
                ox = dc * tab['wx'] + dr * tab['hx']
                oy = dr * tab['hy']
                for nb in range(B):
                    for kk in range(cells[nb]['n']):
                        n0 = [cells[nb]['v'][kk][0] + ox, cells[nb]['v'][kk][1] + oy]
                        n1 = [cells[nb]['v'][(kk + 1) % cells[nb]['n']][0] + ox,
                              cells[nb]['v'][(kk + 1) % cells[nb]['n']][1] + oy]
                        if abs(n0[0] - Bv[0]) < TOL and abs(n0[1] - Bv[1]) < TOL and \
                           abs(n1[0] - A[0]) < TOL and abs(n1[1] - A[1]) < TOL:
                            be.append((nb, dr, dc))
                            found = True
                            break
                    if found:
                        break
                if found:
                    break
            if found:
                break
        if not found:
            be.append(None)
    edges.append(be)

# 顶点 → 共享格
vert_map = defaultdict(list)
for r in range(-5, 6):
    for c in range(-5, 6):
        for b in range(B):
            ox = c * tab['wx'] + r * tab['hx']
            oy = r * tab['hy']
            for p in cells[b]['v']:
                key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                vert_map[key].append((b, r, c))


def cc_center(cc):
    b, r, c = cc
    return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
            cells[b]['cy'] + r * tab['hy'])


def neighbors(cc):
    b, r, c = cc
    out = []
    for e in edges[b]:
        if e is None:
            continue
        out.append((e[0], r + e[1], c + e[2]))
    return out


# 锚格 b 的朝向变换：用"邻格中心偏移集"匹配同边数参考格（b=0 三角形）
# 简化：直接比较锚格 b 与参考格 b0 的邻格中心偏移集，找旋转/镜像
def transform_to_ref(b, refb):
    """把锚格 b 的坐标系变换到 refb 坐标系：返回 (rot_deg, mirror)。
    用邻格中心偏移集匹配：refb 的邻格偏移集 vs b 的邻格偏移集（旋转/镜像后）。"""
    def offs(bb):
        s = []
        c0x, c0y = cells[bb]['cx'], cells[bb]['cy']
        for e in edges[bb]:
            if e is None:
                continue
            cx0, cy0 = cc_center((e[0], e[1], e[2]))
            s.append((round(cx0 - c0x, 4), round(cy0 - c0y, 4)))
        return sorted(s)

    ref = offs(refb)
    mine = offs(b)
    for mirror in [False, True]:
        for rot90 in range(4):
            ang = rot90 * 90.0
            t = []
            for x, y in mine:
                if mirror:
                    y = -y
                a = math.radians(ang)
                t.append((round(x * math.cos(a) - y * math.sin(a), 4),
                          round(x * math.sin(a) + y * math.cos(a), 4)))
            if sorted(t) == ref:
                return (ang, mirror)
    return None


# Config 形状表（Arch31212 L9 = shape 3, L10 = shape 4）
L9 = [(-1.074569936353, 0.0), (-0.537284965912, -0.930604859102),
      (-0.537284965912, 0.930604740898), (0.0, 0.0),
      (0.537284965912, -0.930604859102), (0.537284965912, 0.930604740898),
      (1.074569931824, 0.0)]
L10 = [(-2.686424834088, -0.930604859102), (-2.686424834088, 0.930604740898),
       (-2.149139868177, -1.8612096), (-2.149139868177, 0.0),
       (-2.149139868177, 1.8612096), (-1.611854897736, -0.930604859102),
       (-1.611854897736, 0.930604740898), (-1.074569931824, -1.8612096),
       (-1.074569931824, 0.0), (-1.074569931824, 1.8612096),
       (-0.537284965912, -0.930604859102), (-0.537284965912, 0.930604740898),
       (0.0, 0.0), (0.537284965912, -0.930604859102),
       (0.537284965912, 0.930604740898), (1.074569931823, 0.0)]


def apply_shape(anchor_b, shape, refb):
    """在锚格 anchor_b 上解析形状：运行时偏移 (ox,oy)=(-dy,dx) → 锚格朝向变换。
    变换：offset_in_anchor = T_anchor ∘ T_ref⁻¹ (offset_in_ref)。
    这里把形状偏移（参考朝向）变换到锚格朝向：T_anchor ∘ T_ref⁻¹。
    简化：T = 旋转(rot) ∘ 镜像。"""
    tr_ref = transform_to_ref(refb, refb)  # 参考格自身 = 恒等
    tr_anchor = transform_to_ref(anchor_b, refb)  # 锚格相对参考的变换
    # T_anchor ∘ T_ref⁻¹ = T_anchor（T_ref 恒等）
    ang, mirror = tr_anchor if tr_anchor else (0.0, False)
    ax, ay = cells[anchor_b]['cx'], cells[anchor_b]['cy']
    hits = []
    for dx, dy in shape:
        ox, oy = -dy, dx  # 运行时旋转
        if mirror:
            oy = -oy
        a = math.radians(ang)
        rx = ox * math.cos(a) - oy * math.sin(a)
        ry = ox * math.sin(a) + oy * math.cos(a)
        # 找该点的格子
        best = None
        bd = 1e9
        for dr in range(-3, 4):
            for dc in range(-3, 4):
                for nb in range(B):
                    c0x = cells[nb]['cx'] + dc * tab['wx'] + dr * tab['hx']
                    c0y = cells[nb]['cy'] + dr * tab['hy']
                    dd = (ax + rx - c0x) ** 2 + (ay + ry - c0y) ** 2
                    if dd < bd:
                        bd = dd
                        best = (nb, dr, dc)
        hits.append((best, round(rx, 4), round(ry, 4)))
    return hits, tr_anchor


ax0, ay0 = cells[0]['cx'], cells[0]['cy']

for shape_name, shape in [('L9', L9), ('L10', L10)]:
    print('=' * 60)
    print(shape_name)
    for anchor_b in [0, 3, 4, 5]:  # 三角形基础格
        hits, tr = apply_shape(anchor_b, shape, 0)
        # 解析出的格子 rel 坐标
        rels = []
        types = []
        for (nb, dr, dc), rx, ry in hits:
            c0x = cells[nb]['cx'] + dc * tab['wx'] + dr * tab['hx']
            c0y = cells[nb]['cy'] + dr * tab['hy']
            rels.append((round(c0x - ax0, 4), round(c0y - ay0, 4), cells[nb]['n']))
            types.append(cells[nb]['n'])
        print('  anchor b=%d 变换=%s 类型构成=%s' % (anchor_b, tr, sorted(types)))
        for r in sorted(rels):
            print('     rel=(%.4f, %.4f) n=%d' % r)
