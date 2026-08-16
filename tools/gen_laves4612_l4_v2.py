#!/usr/bin/env python3
"""构造 Laves4612 L4/L4b 的正确菱形：两变体应"组成的菱形实际一致"。

L4 变体1 = 2级城（锚+斜边格）基础上加两个长直角边邻格。
L4b 变体2 = 四个三角形直角顶点共顶点。
文档：两变体组成的菱形一致，只是内部三角形排列不同 → 应为同一组 4 格。

L2 = 锚(b=0) + 共享斜边格（斜边竖直）。锚 = (0,0)。
锚的边：x=1.0746(30°短直角边)、z=1.8612(-60°长直角边)、y=2.1491(-90°斜边)。
"""
import json
import math
from collections import defaultdict

TOL = 1e-6

d = json.load(open('data/tiling_specs_laves.json', encoding='utf-8'))
j = d['laves_4612']
old_wx, old_hy = j['W'][0], j['H'][1]
cells = []
for c in j['cells']:
    nc = old_hy - c['cy']
    ny = c['cx']
    v = [[old_hy - p[1], p[0]] for p in c['v']]
    cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
tab = {'wx': old_hy, 'hx': 0.0, 'hy': old_wx}
B = len(cells)

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


def cc_center(cc):
    b, r, c = cc
    return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
            cells[b]['cy'] + r * tab['hy'])


def rel(cc, ax, ay):
    cx0, cy0 = cc_center(cc)
    return (round(cx0 - ax, 4), round(cy0 - ay, 4))


def edge_neighbor(cc, k):
    b, r, c = cc
    e = edges[b][k]
    if e is None:
        return None
    return (e[0], r + e[1], c + e[2])


def edge_info(b, k):
    A = cells[b]['v'][k]
    Bv = cells[b]['v'][(k + 1) % cells[b]['n']]
    dx, dy = Bv[0] - A[0], Bv[1] - A[1]
    return math.hypot(dx, dy)


ax, ay = cells[0]['cx'], cells[0]['cy']
anchor = (0, 0, 0)

# 锚的边信息
print('锚格 b=0 三边（len, 方向角）:')
for k in range(3):
    ln = edge_info(0, k)
    A = cells[0]['v'][k]
    Bv = cells[0]['v'][(k + 1) % 3]
    dx, dy = Bv[0] - A[0], Bv[1] - A[1]
    ang = math.degrees(math.atan2(dy, dx)) % 180
    if ang > 90:
        ang -= 180
    nb = edge_neighbor(anchor, k)
    print('  边%d len=%.4f 角=%.1f 邻格=%s' % (k, ln, ang,
          rel(nb, ax, ay) if nb else None))

# 斜边 = 最长边
lens = sorted((edge_info(0, k), k) for k in range(3))
maxk = lens[-1][1]  # 斜边
zk = lens[-2][1]    # 长直角边
xk = lens[0][1]     # 短直角边
slope = edge_neighbor(anchor, maxk)
print('\n斜边格:', rel(slope, ax, ay))
print('锚长直角边邻格:', rel(edge_neighbor(anchor, zk), ax, ay))
print('锚短直角边邻格:', rel(edge_neighbor(anchor, xk), ax, ay))

# 斜边格的三边
print('\n斜边格三边:')
for k in range(3):
    ln = edge_info(slope[0], k)
    nb = edge_neighbor(slope, k)
    print('  边 len=%.4f 邻格=%s' % (ln, rel(nb, ax, ay) if nb else None))

# L4 变体1：锚 + 斜边格 + 锚长直角边邻格 + 斜边格长直角边邻格
slope_lens = sorted((edge_info(slope[0], k), k) for k in range(3))
slope_zk = slope_lens[-2][1]
l4 = set([anchor, slope,
          edge_neighbor(anchor, zk),
          edge_neighbor(slope, slope_zk)])
print('\nL4 变体1 格子（锚+斜边+两长直角边邻格）:')
for cc in sorted(l4, key=lambda c: (rel(c, ax, ay)[1], rel(c, ax, ay)[0])):
    print('  rel=(%.4f,%.4f)' % (rel(cc, ax, ay)[0], rel(cc, ax, ay)[1]))

# 轮廓（凸包）
def poly_of(cc):
    b, r, c = cc
    out = []
    for p in cells[b]['v']:
        out.append((p[0] + c * tab['wx'] + r * tab['hx'] - ax,
                    p[1] + r * tab['hy'] - ay))
    return out

def convex_hull(pts):
    pts = sorted(set((round(x, 6), round(y, 6)) for x, y in pts))
    if len(pts) <= 3:
        return pts
    def cross(o, a, b):
        return (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])
    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]

allp = []
for cc in l4:
    allp.extend(poly_of(cc))
print('L4 变体1 轮廓顶点:', [(round(x, 4), round(y, 4)) for x, y in convex_hull(allp)])
print('L4 变体1 config:', sorted((round(ry, 12), round(-rx, 12)) for rx, ry in
      [(rel(cc, ax, ay)[0], rel(cc, ax, ay)[1]) for cc in l4]))
