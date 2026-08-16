#!/usr/bin/env python3
"""通用 Laves 形状构造器：按开发思路.txt 语义用顶点价数/边邻接枚举形状格集合。

思路：Laves 密铺只有一种面。文档形状 = "某顶点附近全部 N 个面" 或 "某面 + 其邻面"。
对每种密铺，构造参考格 b=0 为中心的全部形状，输出 Config 存储偏移。

形状定义（按文档）：
- Laves31212（等腰三角形）: L2=菱形(2格), L3=等边三角(3格,顶角共点), L6=六边形(6格),
  L6=菱形(2×L3), L12=雪花(12格)
- Laves3464（筝形）: L1=1, L3=3, L6=6, L9=9
- Laves3636（菱形）: L1=1, L3=3, L5=5, L6=6, L12=12
- Laves4612（不等边三角形）: L2=2, L4=4, L4=4, L6=6, L12=12, L12=12
- Laves488（等腰直角三角形）: L2=2, L4=4, L8=8

实现：对每个密铺，枚举参考格 b=0 的顶点（含价数），按文档语义组合。
"""
import json
import math
from collections import defaultdict, deque

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


def main():
    for name, spec_key in [('laves_31212', 'laves_31212'), ('laves_3636', 'laves_3636'),
                           ('laves_488', 'laves_488')]:
        tab, cells = load(spec_key, 'data/tiling_specs_laves.json')
        edges = build_neighbors(tab, cells)
        B = len(cells)
        vert_map = defaultdict(list)
        for r in range(-6, 7):
            for c in range(-6, 7):
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

        def rel(cc):
            cx0, cy0 = cc_center(cc)
            ax, ay = cc_center((0, 0, 0))
            return (cx0 - ax, cy0 - ay)

        def neighbors(cc):
            b, r, c = cc
            out = []
            for e in edges[b]:
                if e is None:
                    continue
                nb, dr, dc = e
                out.append((nb, r + dr, c + dc))
            return out

        def to_config(offs):
            out = []
            for rx, ry in offs:
                out.append((round(ry, 12), round(-rx, 12)))
            seen = set()
            res = []
            for o in out:
                if o in seen:
                    continue
                seen.add(o)
                res.append(o)
            return res

        def fmt(offs):
            return '{' + ', '.join('{%.12f, %.12f, 0}' % o for o in offs) + '}'

        print('====', name, 'B=%d' % B)
        # 参考格 b=0 的顶点及价数
        anchor = (0, 0, 0)
        ax, ay = cc_center(anchor)
        print('参考格 b=0 顶点:')
        for p in cells[0]['v']:
            key = (round(p[0] * 1e4), round(p[1] * 1e4))
            shared = vert_map.get(key, [])
            print('  rel=(%.4f,%.4f) 价数=%d' % (p[0] - ax, p[1] - ay, len(shared)))
            for cc in shared[:20]:
                r = rel(cc)
                print('    %s rel=(%.4f, %.4f)' % (cc, r[0], r[1]))


if __name__ == '__main__':
    main()
