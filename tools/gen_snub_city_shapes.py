#!/usr/bin/env python3
"""gen_snub_city_shapes.py — 生成 4 种扭棱密铺 (arch/laves 3.3.3.3.6、3.3.4.3.4) 城市形状表。

原理：形状格 = 参考基础格（mask 最低置位 / 该边数首格）局部邻域的实际格；
存储偏移 = rot90⁻¹(c_i − c_ref)，锚格恒 (0,0)。放置时经 applyAnchorOrientation 旋转到锚格帧。

用法: python tools/gen_snub_city_shapes.py [--verify]
--verify 时对每种形状在全图各锚格做运行时管线模拟并核对模式。
"""
import json
import math
import sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

# ---------------------------------------------------------------------------
# 工具
# ---------------------------------------------------------------------------
def vertex_cells(tab, p, radius=3):
    """共享物理顶点 p 的全部格 (b,r,c)。"""
    B = len(tab['cells'])
    out = []
    for b in range(B):
        for r in range(-radius, radius + 1):
            for c in range(-radius, radius + 1):
                ox = c * tab['wx'] + r * tab['hx']
                oy = c * tab['wy'] + r * tab['hy']
                for v in tab['cells'][b]['v']:
                    if same((v[0] + ox, v[1] + oy), p):
                        out.append((b, r, c))
                        break
    return out

def cabs(tab, cc):
    b, r, c = cc
    cell = tab['cells'][b]
    return (cell['cx'] + c * tab['wx'] + r * tab['hx'],
            cell['cy'] + c * tab['wy'] + r * tab['hy'])

def edge_lens(tab, b):
    cell = tab['cells'][b]
    return [math.hypot(cell['v'][(k + 1) % cell['n']][0] - cell['v'][k][0],
                       cell['v'][(k + 1) % cell['n']][1] - cell['v'][k][1])
            for k in range(cell['n'])]

def horiz_edge_idx(tab, b):
    cell = tab['cells'][b]
    for k in range(cell['n']):
        if abs(cell['v'][k][1] - cell['v'][(k + 1) % cell['n']][1]) < 1e-4:
            return k
    return -1

def bcorner_vertex(tab, b):
    """laves 五边形两条长边的公共顶点（长边角）。"""
    lens = edge_lens(tab, b)
    long_k = [k for k, l in enumerate(lens) if l > 0.6 * max(lens)]
    cell = tab['cells'][b]
    n = cell['n']
    # 两长边 [k1, k2] 的公共顶点：边 k 从 v[k] 到 v[(k+1)%n]
    k1, k2 = long_k
    e1_end = (k1 + 1) % n
    if e1_end == k2:
        return cell['v'][k2]
    if (k2 + 1) % n == k1:
        return cell['v'][k1]
    return cell['v'][e1_end]

def tri_apex(tab, b):
    """arch 三角形顶角顶点（水平边对侧）。"""
    cell = tab['cells'][b]
    hk = horiz_edge_idx(tab, b)
    if hk < 0:
        return None
    return cell['v'][(hk + 2) % 3]

# ---------------------------------------------------------------------------
# 形状推导
# ---------------------------------------------------------------------------
def derive_arch33336():
    tab = load_spec('arch_33336', 'data/tiling_specs_arch.json')
    edges = build_edges(tab)
    shapes = []
    # L1: 一正一反两三角, 公共边水平。锚 = 参考格 base0 (DOWN) + 其水平边邻 (base2)。
    # 方向锚定：偏移 (0, +0.7035) 竖直 → 锚格 T ∈ {0°,180°}：bases 0,2,3,5,8,13。
    shapes.append(('L1', 0, 3, 0x212D, [(0, 0, 0), (2, 0, 0)]))
    # L3: 4 小三角大三角形；锚 = 中心三角（全三角邻域）bases 2,3,10,11。
    shapes.append(('L3', 1, 3, 0x0C0C, [(2, 0, 0), (0, 0, 0), (1, 0, 0), (6, 0, 0)]))
    # L4: 一个正六边形。
    shapes.append(('L4', 2, 6, 0, [(16, 0, 0)]))
    # L5: 六边形 + 正上/正下两个三角。
    shapes.append(('L5', 3, 6, 0, [(16, 0, 0), (7, 0, 0), (14, 0, -1)]))
    # L9: 六边形 + 左右各 4 三角。
    shapes.append(('L9', 4, 6, 0, [(16, 0, 0), (0, 0, 0), (10, 0, -1), (12, 0, -1),
                                (13, 0, -1), (5, 0, 0), (8, 0, 0), (9, 0, 0), (11, 0, 0)]))
    return {
        'file': 'data/tiling_specs_arch.json', 'name': 'arch_33336',
        'levels': [9.0 / 7.0, 18.0 / 7.0, 27.0 / 7.0, 36.0 / 7.0, 9.0],
        'iconLevels': [1, 3, 4, 5, 9],
        'shapes': shapes,
    }

def derive_arch33434():
    tab = load_spec('arch_33434', 'data/tiling_specs_arch.json')
    edges = build_edges(tab)
    B = len(tab['cells'])
    # L5a 正: 参考格 base2 (DOWN, 顶点 (1.732,0.634)) 的顶点 5 邻
    apex2 = tri_apex(tab, 2)
    five2 = sorted(vertex_cells(tab, apex2), key=lambda cc: (cabs(tab, cc)[1], cabs(tab, cc)[0]))
    # 锚格 = base 2 (5 邻中含 base2 的格)
    l5a = [(cc) for cc in five2 if cc[0] == 2] + [cc for cc in five2 if cc[0] != 2]
    # L5b 反: 参考格 base3 (UP, 顶点 (1.732,2.83)) 的顶点 5 邻
    apex3 = tri_apex(tab, 3)
    five3 = sorted(vertex_cells(tab, apex3), key=lambda cc: (cabs(tab, cc)[1], cabs(tab, cc)[0]))
    l5b = [cc for cc in five3 if cc[0] == 3] + [cc for cc in five3 if cc[0] != 3]
    return {
        'file': 'data/tiling_specs_arch.json', 'name': 'arch_33434',
        'levels': [0.6961524227066321, 1.6076951545867362, 5.303847577293366, 12.0],
        'iconLevels': [1, 2, 5, 12],
        'shapes': [
            ('L1', 0, 3, 0x30C, [(2, 0, 0)]),
            ('L2', 1, 4, 0, [(4, 0, 0)]),
            ('L5a', 2, 3, 0x104, l5a),
            ('L5b', 2, 3, 0x208, l5b),
            # L12a: 边邻三角对 (2,3) 及所有共点格；水平公共边对还有 (8,9)
            ('L12a', 3, 3, 0x30C, [(2, 0, 0), (3, 0, 0), (0, 0, 0), (1, 0, 0),
                                (4, 0, 0), (5, 0, 0), (6, 0, 0), (6, 1, 0),
                                (7, 0, 0), (7, 1, 0), (10, 0, 0), (11, 0, 0)]),
            # L12b: 边邻三角对 (0@0c, 1@1c) 及所有共点格；竖直公共边对还有 (6,7)
            ('L12b', 3, 3, 0xC3, [(0, 0, 0), (1, 0, 1), (2, 0, 0), (2, 0, 1),
                               (3, 0, 0), (3, 0, 1), (4, 0, 1), (5, 0, 1),
                               (8, 0, 0), (9, 0, 0), (10, 0, 0), (11, 0, 0)]),
        ],
    }

def derive_laves33336():
    tab = load_spec('laves_33336', 'data/tiling_specs_laves.json')
    edges = build_edges(tab)
    # base0 的 b-corner 处花朵
    V = bcorner_vertex(tab, 0)
    flower = sorted(vertex_cells(tab, V), key=lambda cc: math.atan2(cabs(tab, cc)[1] - V[1],
                                                                   cabs(tab, cc)[0] - V[0]))
    upper = [cc for cc in flower if cabs(tab, cc)[1] > V[1]]
    lower = [cc for cc in flower if cabs(tab, cc)[1] < V[1]]
    anchor0 = [cc for cc in flower if cc[0] == 0]
    assert len(flower) == 6 and len(upper) == 3 and len(lower) == 3
    return {
        'file': 'data/tiling_specs_laves.json', 'name': 'laves_33336',
        'levels': [1.0, 3.0, 6.0],
        'shapes': [
            # L1: 左右对称五边形 (b 边不水平, 竖轴对称) bases 4,5,10,11
            ('L1', 0, 5, 0x0C30, [(4, 0, 0)]),
            # L3a 上: 参考格 base0 + 上两格
            ('L3a', 1, 5, 0x881, anchor0 + [cc for cc in upper if cc[0] != 0]),
            # L3b 下: 参考格 = 下花朵中最小的 base (base3)；锚 = base3 的下花朵
            ('L3b', 1, 5, 0x118, [cc for cc in lower if cc[0] == 3] +
                              [cc for cc in lower if cc[0] != 3]),
            # L6: 全部 6 格（锚 = base0）
            ('L6', 2, 5, 0x999, anchor0 + [cc for cc in flower if cc[0] != 0]),
        ],
    }

def derive_laves33434():
    tab = load_spec('laves_33434', 'data/tiling_specs_laves.json')
    edges = build_edges(tab)
    return {
        'file': 'data/tiling_specs_laves.json', 'name': 'laves_33434',
        'levels': [1.0, 4.0, 12.0],
        'shapes': [
            ('L1', 0, 5, 0xF0, [(4, 0, 0)]),
            ('L4a', 1, 5, 0x0F, [(0, 0, 0), (2, 0, -1), (4, 0, 0), (6, -1, 0)]),
            ('L4b', 1, 5, 0xF0, [(4, 0, 0), (3, 0, 0), (6, 0, 0), (1, 0, -1)]),
            ('L12a', 2, 5, 0x0F, [(0, 0, 0), (2, 0, -1), (4, 0, 0), (6, -1, 0),
                               (1, -1, -1), (1, 0, -1), (3, -1, 0), (3, 0, 0),
                               (5, -1, -1), (5, -1, 0), (7, 0, -1), (7, 0, 0)]),
            ('L12b', 2, 5, 0xF0, [(4, 0, 0), (3, 0, 0), (6, 0, 0), (1, 0, -1),
                               (0, 0, 0), (0, 1, 0), (2, 0, -1), (2, 1, -1),
                               (5, 0, -1), (5, 0, 0), (7, 0, -1), (7, 0, 0)]),
        ],
    }

# ---------------------------------------------------------------------------
# 生成 + 验证
# ---------------------------------------------------------------------------
def emit(t):
    tab = load_spec(t['name'], t['file'])
    edges = build_edges(tab)
    tr, first = compute_anchor_transforms(tab, edges)
    ref_of = {}
    for anchorN in sorted(set(c['n'] for c in tab['cells'])):
        ref_of[anchorN] = first[anchorN]
    print('// ---- %s 城市形状表 ----' % t['name'])
    levels = ', '.join('%.17g' % lv for lv in t['levels'])
    icons_line = ''
    if t.get('iconLevels'):
        icons_line = '    s.iconLevels = {%s};\n' % ', '.join(str(i) for i in t['iconLevels'])
    print('{')
    print('    auto& s = c.sets[static_cast<size_t>(static_cast<int>(TilingType::%s))];' %
          ''.join(w.capitalize() for w in t['name'].split('_')))
    print('    s.levels = {%s};' % levels)
    print(icons_line, end='')
    print('    s.shapes = {')
    for i, (label, levIdx, anchorN, mask, cells_spec) in enumerate(t['shapes']):
        refb = ref_of[anchorN]
        if mask:
            for bb in range(31):
                if mask & (1 << bb):
                    refb = bb
                    break
        refc = cabs(tab, (refb, 0, 0))
        stored = []
        for cc in cells_spec:
            if cc == cells_spec[0]:
                stored.append((0.0, 0.0))
                continue
            cx0, cy0 = cabs(tab, cc)
            dx, dy = cx0 - refc[0], cy0 - refc[1]
            stored.append((round(dy, 12), round(-dx, 12)))
        mask_str = '' if mask == 0 else ', 0x%x' % mask
        cells = ', '.join('{%.12f, %.12f, 0}' % s for s in stored)
        print('        shp(%d, {%s}%s),  // %s' % (anchorN, cells, mask_str, label))
    print('    };')
    print('    s.shapeLevelIndex = {%s};' % ', '.join(str(li) for _, li, _, _, _ in t['shapes']))
    print('}')
    print()

def main():
    tilings = [derive_arch33336(), derive_arch33434(), derive_laves33336(),
               derive_laves33434()]
    for t in tilings:
        emit(t)
    if '--verify' in sys.argv:
        import verify_snub_shapes as vs
        print('// verify')
        for t in tilings:
            tab = load_spec(t['name'], t['file'])
            edges = build_edges(tab)
            print('== %s ==' % t['name'])
            for label, levIdx, anchorN, mask, cells_spec in t['shapes']:
                anchors = sorted(b for b in range(len(tab['cells']))
                                 if mask == 0 or (mask & (1 << b)))
                ok = vs.verify_shape(tab, edges, mask, cells_spec, t['name'], label)
                print('  %s: %s' % (label, 'OK' if ok else 'FAIL'))

if __name__ == '__main__':
    main()
