#!/usr/bin/env python3
"""综合分析 Laves 密铺几何：锚格 b=0 的顶点价数、边方向、周围格，辅助构造文档语义形状。

输出每个密铺：
- 锚格 b=0 各顶点：rel 坐标、价数、共享格列表（含锚格自身）
- 锚格 b=0 各边：方向（dx,dy）、长度、共享该边的邻格
- 锚格全部邻格（共享边）：rel 坐标
- 所有顶点价数分布
"""
import json
import math
from collections import defaultdict

TOL = 1e-6


def load(name, file):
    d = json.load(open(file, encoding='utf-8'))
    j = d[name]
    old_wx = j['W'][0]
    old_hy = j['H'][1]
    cells = []
    for c in j['cells']:
        nc = old_hy - c['cy']
        ny = c['cx']
        v = [[old_hy - p[1], p[0]] for p in c['v']]
        cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
    return {'wx': old_hy, 'hx': 0.0, 'hy': old_wx}, cells


def build_neighbors(tab, cells):
    B = len(cells)
    edges = []
    for b in range(B):
        cell = cells[b]
        n = cell['n']
        be = []
        for k in range(n):
            A = cell['v'][k]
            Bv = cell['v'][(k + 1) % n]
            found = False
            for dr in range(-2, 3):
                for dc in range(-2, 3):
                    ox = dc * tab['wx'] + dr * tab['hx']
                    oy = dr * tab['hy']
                    for nb in range(B):
                        ocell = cells[nb]
                        for kk in range(ocell['n']):
                            n0 = [ocell['v'][kk][0] + ox, ocell['v'][kk][1] + oy]
                            n1 = [ocell['v'][(kk + 1) % ocell['n']][0] + ox,
                                  ocell['v'][(kk + 1) % ocell['n']][1] + oy]
                            if (n0[0] - Bv[0]) ** 2 + (n0[1] - Bv[1]) ** 2 <= TOL * TOL and \
                               (n1[0] - A[0]) ** 2 + (n1[1] - A[1]) ** 2 <= TOL * TOL:
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
    return edges


def build_vert_map(tab, cells, radius=6):
    B = len(cells)
    vert_map = defaultdict(list)
    for r in range(-radius, radius + 1):
        for c in range(-radius, radius + 1):
            for b in range(B):
                ox = c * tab['wx'] + r * tab['hx']
                oy = r * tab['hy']
                for p in cells[b]['v']:
                    key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                    vert_map[key].append((b, r, c))
    return vert_map


def main():
    for spec_key in ['laves_31212', 'laves_4612', 'laves_3464', 'laves_488']:
        tab, cells = load(spec_key, 'data/tiling_specs_laves.json')
        edges = build_neighbors(tab, cells)
        vert_map = build_vert_map(tab, cells)
        B = len(cells)
        ax, ay = cells[0]['cx'], cells[0]['cy']

        def cc_center(cc):
            b, r, c = cc
            return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
                    cells[b]['cy'] + r * tab['hy'])

        def rel(cc):
            cx0, cy0 = cc_center(cc)
            return (round(cx0 - ax, 4), round(cy0 - ay, 4))

        print('=' * 70)
        print('==== %s ====' % spec_key)
        # 价数分布
        val_counts = defaultdict(int)
        for key, shared in vert_map.items():
            val_counts[len(shared)] += 1
        print('价数分布:', dict(sorted(val_counts.items())))
        # 锚格顶点
        print('锚格 b=0 顶点:')
        for p in cells[0]['v']:
            key = (round(p[0] * 1e4), round(p[1] * 1e4))
            shared = vert_map.get(key, [])
            print('  顶点 rel=(%.4f, %.4f) 价数=%d 共享格:' % (p[0] - ax, p[1] - ay, len(shared)))
            for cc in shared:
                print('     %s rel=%s' % (str(cc), rel(cc)))
        # 锚格边 + 邻格
        print('锚格 b=0 边 (端点 -> 方向/长度 -> 邻格):')
        for k in range(cells[0]['n']):
            A = cells[0]['v'][k]
            Bv = cells[0]['v'][(k + 1) % cells[0]['n']]
            dx, dy = Bv[0] - A[0], Bv[1] - A[1]
            ln = math.sqrt(dx * dx + dy * dy)
            e = edges[0][k]
            nb = rel(e) if e is not None else None
            print('  边%d 端点(%.3f,%.3f)->(%.3f,%.3f) dir=(%.3f,%.3f) len=%.4f 邻格=%s'
                  % (k, A[0] - ax, A[1] - ay, Bv[0] - ax, Bv[1] - ay, dx, dy, ln, nb))
        # 锚格 2-hop 邻（共享边/顶点的格，去重）
        print('锚格 2-hop 邻格（含锚）:')
        seen = {(0, 0, 0)}
        frontier = [(0, 0, 0)]
        for _ in range(2):
            nf = set()
            for cc in frontier:
                for k, e in enumerate(edges[cc[0]]):
                    if e is None:
                        continue
                    nf.add((e[0], cc[1] + e[1], cc[2] + e[2]))
            seen |= nf
            frontier = list(nf)
        for cc in sorted(seen, key=lambda c: (c[1], c[2], c[0])):
            print('   %s rel=%s' % (str(cc), rel(cc)))


if __name__ == '__main__':
    main()
