#!/usr/bin/env python3
"""完整分析 Laves31212：12 个基础格、邻接、顶点共点关系，构造全部形状。"""
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
                            if same_point(n0, Bv) and same_point(n1, A):
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


def same_point(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 <= TOL * TOL


def main():
    tab, cells = load('laves_31212', 'data/tiling_specs_laves.json')
    edges = build_neighbors(tab, cells)
    B = len(cells)
    print('B=%d, W=(%.4f,0) H=(%.4f,%.4f)' % (B, tab['wx'], tab['hx'], tab['hy']))
    # 12 个基础格的中心和多边形
    for b in range(B):
        v = cells[b]['v']
        print('base %2d center=(%.4f, %.4f) poly=' % (b, cells[b]['cx'], cells[b]['cy']),
              [(round(p[0], 4), round(p[1], 4)) for p in v])
    # 邻接
    for b in range(B):
        print('base %2d neighbors:' % b, end='')
        for e in edges[b]:
            if e is None:
                print(' -', end='')
                continue
            print(' (%d,%d,%d)' % e, end='')
        print()


if __name__ == '__main__':
    main()
