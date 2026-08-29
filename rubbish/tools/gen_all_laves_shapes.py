#!/usr/bin/env python3
"""以参考格 b=0 为锚，按开发思路.txt 语义精确生成全部 Laves 形状表（第 2 版）。

原则：形状表锚 = 同边数第一个基础格 b=0（与 TilingGeom::shapeReferenceBase 一致），
偏移 = 形状内其他格中心 - b=0 中心（运行时坐标），输出 Config 存储格式 (ry, -rx)。

文档语义（顶点价数枚举，已由 analyze_laves_topology.py 确认）：
- Laves3636（菱形）: L1={b0}, L3=3-valent顶点周围3格, L5=b0+4边邻, L6=6-valent周围6格,
  L12=6-valent周围6格+边邻=12格（大正六边形）
- Laves31212（等腰三角形，顶角120°=3-valent，底角30°=12-valent）:
  L2=锚+共享水平底边格（菱形）, L3=顶角(3-valent)周围3格（等边三角形）,
  L6a=正L3+边邻（大六边形，顶角朝下）, L6b=反L3+边邻（大六边形，顶角朝上）,
  L6c=正L3+反L3（菱形，公共边水平）, L12=底角(12-valent)周围12格（大雪花）
- Laves3464（筝形）: L1={b0}, L3=3-valent周围3格, L6=6-valent周围6格, L9=L3+边邻
- Laves4612（不等边直角三角形，直角=4-valent，60°角=6-valent，30°角=12-valent）:
  L2=锚+共享斜边格（筝形）, L4=2级城+长直角边邻格（菱形）,
  L4'=直角(4-valent)周围4格（菱形，直角共点）, L6=60°角(6-valent)周围6格（大等边三角形）,
  L12=30°角(12-valent)周围12格（大六边形）, L12'=L6+边邻（12格大六边形）
- Laves488（等腰直角三角形，直角=4-valent）: L2=锚+共享底边格（正方形）,
  L4=直角(4-valent)周围4格（大等腰直角三角形）, L8=L4+边邻（大正方形）

验证：每个形状格数 = 等级整数，无重复格，全部解析成功。
"""
import json
import math
import sys
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


def build_vert_map(tab, cells, radius=8):
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


def generate(tab, cells, edges, vert_map, spec_key):
    B = len(cells)
    anchor = (0, 0, 0)
    ax, ay = cells[0]['cx'], cells[0]['cy']

    def cc_center(cc):
        b, r, c = cc
        return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
                cells[b]['cy'] + r * tab['hy'])

    def rel(cc):
        cx0, cy0 = cc_center(cc)
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

    def shared_verts(cc):
        """格 cc 的顶点（key -> 该顶点共享格列表）。"""
        b, r, c = cc
        out = []
        for p in cells[b]['v']:
            ox = c * tab['wx'] + r * tab['hx']
            oy = r * tab['hy']
            key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
            shared = vert_map.get(key, [])
            out.append({'rel': (p[0] + ox - ax, p[1] + oy - ay), 'val': len(shared),
                        'shared': shared, 'key': key})
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

    def sort_cc(ccs):
        return sorted(ccs, key=lambda cc: (round(rel(cc)[1], 4), round(rel(cc)[0], 4), cc[0]))

    results = {}

    def add(name, ccs, expect=None):
        offs = [rel(cc) for cc in ccs]
        dup = len(offs) - len(set((round(x, 4), round(y, 4)) for x, y in offs))
        cfg = to_config(offs)
        results[name] = {'count': len(offs), 'dup': dup, 'cfg': cfg, 'expect': expect}
        return offs

    if spec_key == 'laves_3636':
        add('L1', [anchor], 1)
        v3 = next((v for v in shared_verts(anchor) if v['val'] == 3), None)
        if v3:
            add('L3', v3['shared'], 3)
        add('L5', [anchor] + neighbors(anchor), 5)
        v6 = next((v for v in shared_verts(anchor) if v['val'] == 6), None)
        if v6:
            add('L6', v6['shared'], 6)
            l6 = set(v6['shared'])
            l12 = set(l6)
            for cc in l6:
                for nb in neighbors(cc):
                    l12.add(nb)
            add('L12', l12, 12)
    elif spec_key == 'laves_31212':
        # L2 = 锚 + 共享水平底边（长对角线）的格
        l2 = [anchor]
        for k, e in enumerate(edges[0]):
            if e is None:
                continue
            A = cells[0]['v'][k]
            Bv = cells[0]['v'][(k + 1) % 3]
            if abs(A[1] - Bv[1]) < 1e-9:  # 水平边 = 底边
                l2.append((e[0], e[1], e[2]))
        add('L2', l2, 2)
        # L3 = 顶角（3-valent，120°）周围 3 格
        v3 = next((v for v in shared_verts(anchor) if v['val'] == 3), None)
        if v3:
            l3 = set(v3['shared'])
            add('L3', l3, 3)
            # L6a = 正 L3 + 边邻（大六边形，中心=锚格顶角）
            l6a = set(l3)
            for cc in l3:
                for nb in neighbors(cc):
                    l6a.add(nb)
            add('L6a', l6a, 6)
            # 反 L3：锚格底边邻格（菱形上半）的顶角（朝上 3-valent）周围 3 格
            # 底边邻格 = L2 中除锚外的格
            opp = [cc for cc in l2 if cc != anchor][0]
            opp_verts = shared_verts(opp)
            v3u = next((v for v in opp_verts if v['val'] == 3), None)
            if v3u:
                l3b = set(v3u['shared'])
                # L6c = 正 L3 + 反 L3（菱形，公共边 = 水平底边）
                add('L6c', l3 | l3b, 6)
                # L6b = 反 L3 + 边邻（大六边形，中心=上方顶角）
                l6b = set(l3b)
                for cc in l3b:
                    for nb in neighbors(cc):
                        l6b.add(nb)
                add('L6b', l6b, 6)
        # L12 = 底角（12-valent，30°）周围 12 格（大雪花）
        v12 = next((v for v in shared_verts(anchor) if v['val'] == 12), None)
        if v12:
            add('L12', v12['shared'], 12)
    elif spec_key == 'laves_3464':
        add('L1', [anchor], 1)
        v3 = next((v for v in shared_verts(anchor) if v['val'] == 3), None)
        if v3:
            add('L3', v3['shared'], 3)
            l3 = set(v3['shared'])
            l9 = set(l3)
            for cc in l3:
                for nb in neighbors(cc):
                    l9.add(nb)
            add('L9', l9, 9)
        v6 = next((v for v in shared_verts(anchor) if v['val'] == 6), None)
        if v6:
            add('L6', v6['shared'], 6)
    elif spec_key == 'laves_4612':
        # L2 = 锚 + 共享斜边（最长边，修正后竖直）的格
        l2 = [anchor]
        maxlen = -1
        maxk = -1
        for k in range(3):
            A = cells[0]['v'][k]
            Bv = cells[0]['v'][(k + 1) % 3]
            d = (A[0] - Bv[0]) ** 2 + (A[1] - Bv[1]) ** 2
            if d > maxlen:
                maxlen = d
                maxk = k
        e = edges[0][maxk]
        if e is not None:
            l2.append((e[0], e[1], e[2]))
        add('L2', l2, 2)
        # L4 = L2 + 两个三角形的长直角边邻格
        # 长直角边 = 次长边（z=1.8612，非斜边非短边）
        def edge_neighbor(cc, k):
            b, r, c = cc
            e = edges[b][k]
            if e is None:
                return None
            return (e[0], r + e[1], c + e[2])

        def long_leg_neighbor(cc):
            """找格 cc 的长直角边（次长边）邻格。"""
            b, r, c = cc
            cell = cells[b]
            lens = []
            for k in range(3):
                A = cell['v'][k]
                Bv = cell['v'][(k + 1) % 3]
                d = (A[0] - Bv[0]) ** 2 + (A[1] - Bv[1]) ** 2
                lens.append((d, k))
            lens.sort(reverse=True)
            k2 = lens[1][1]  # 次长边 = 长直角边
            return edge_neighbor(cc, k2)

        l4 = set(l2)
        for cc in l2:
            nb = long_leg_neighbor(cc)
            if nb is not None:
                l4.add(nb)
        add('L4', l4, 4)
        # L4' = 直角（4-valent）周围 4 格
        v4 = next((v for v in shared_verts(anchor) if v['val'] == 4), None)
        if v4:
            add('L4b', v4['shared'], 4)
        # L6 = 60°角（6-valent，长直角边对角）周围 6 格 = 大等边三角形
        v6 = next((v for v in shared_verts(anchor) if v['val'] == 6), None)
        if v6:
            add('L6', v6['shared'], 6)
        # L12 = 30°角（12-valent，短直角边对角）周围 12 格 = 大六边形
        v12 = next((v for v in shared_verts(anchor) if v['val'] == 12), None)
        if v12:
            l12a = v12['shared']
            add('L12a', l12a, 12)
            # L12b = L6 + 全部边邻 = 12 格大六边形
            l6set = set(v6['shared'])
            l12b = set(l6set)
            for cc in l6set:
                for nb in neighbors(cc):
                    l12b.add(nb)
            add('L12b', l12b, 12)
    elif spec_key == 'laves_488':
        add('L1', [anchor], 1)
        # L2 = 锚 + 共享底边（最长边）格 = 正方形
        l2 = [anchor]
        maxlen = -1
        maxk = -1
        for k in range(3):
            A = cells[0]['v'][k]
            Bv = cells[0]['v'][(k + 1) % 3]
            d = (A[0] - Bv[0]) ** 2 + (A[1] - Bv[1]) ** 2
            if d > maxlen:
                maxlen = d
                maxk = k
        e = edges[0][maxk]
        if e is not None:
            l2.append((e[0], e[1], e[2]))
        add('L2', l2, 2)
        # L4 = 直角（4-valent）周围 4 格 = 大等腰直角三角形
        v4 = next((v for v in shared_verts(anchor) if v['val'] == 4), None)
        if v4:
            l4 = set(v4['shared'])
            add('L4', l4, 4)
            # L8 = L4 + 边邻 = 大正方形
            l8 = set(l4)
            for cc in l4:
                for nb in neighbors(cc):
                    l8.add(nb)
            add('L8', l8, 8)

    print('==== %s ====' % spec_key)
    for lvl, r in sorted(results.items()):
        mark = ''
        if r['expect'] is not None and r['count'] != r['expect']:
            mark = '  <<<< 格数!=等级(%d)' % r['expect']
        if r['dup']:
            mark += '  <<<< 重复格!'
        print('%s: %d 格 (重复 %d)%s' % (lvl, r['count'], r['dup'], mark))
        print('   config=%s' % fmt(r['cfg']))


def main():
    for spec_key in ['laves_3636', 'laves_31212', 'laves_3464', 'laves_488', 'laves_4612']:
        tab, cells = load(spec_key, 'data/tiling_specs_laves.json')
        edges = build_neighbors(tab, cells)
        vert_map = build_vert_map(tab, cells)
        generate(tab, cells, edges, vert_map, spec_key)


if __name__ == '__main__':
    main()
