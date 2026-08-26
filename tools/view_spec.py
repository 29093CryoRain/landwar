#!/usr/bin/env python3
# view_spec.py — 渲染某密铺的单个平移块（基于 tiling_specs_*.json 数据），
# 并标注每个顶点的 "valency"（密铺到全平面后有多少个基础格在该点相交）。
# 用途：肉眼核对 spec 块的拓扑/方向是否正确，并据 valency 判断哪些顶点可作城市中心。
#
# 用法:
#   python tools/view_spec.py                       # 交互菜单列出所有密铺
#   python tools/view_spec.py <name> [out]          # 渲染指定密铺, 存到默认目录 block_<name>.png
#   python tools/view_spec.py --all [-o DIR]        # 渲染所有密铺到目录 DIR
#   python tools/view_spec.py --list                # 列出可用密铺名
#
# 图面: 中央为块 (i,j)=0,0 的格（着色 + 下标），周围画一圈邻居块 (轻灰) 作 context，
#       使跨块 overhang 的格 "显示全"。坐标轴范围取中央块格的真实跨距（不裁剪）。
#
# valency 与城市的关系（对 laves_4.6.12 / 3.x 类，角类型 = 对边决定）:
#   valency 6  (60° 顶点, 6 格相交)  -> 可作 L12 城中心
#   valency 12 (30° 顶点, 12 格相交) -> 可作 三角形L6 / 另一L12 中心
#   valency 4  (90° 顶点, 4 格相交)  -> 可作 矩形L6 / L4 中心
import json, math, sys, os, collections
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MPoly

ARCH = 'data/tiling_specs_arch.json'
LAVES = 'data/tiling_specs_laves.json'
DEF_DIR = 'tools/blocks'
RING = 1  # 中央块四周画的邻居圈数

def load_all():
    d = {}
    for f in (ARCH, LAVES):
        d.update(json.load(open(f, encoding='utf-8')))
    return d

def valency_map(spec, nb=2):
    W = spec['W']; H = spec['H']
    Wv = (W[0], W[1]); Hv = (H[0], H[1])
    cells = spec['cells']
    verts = {}
    for i in range(-nb, nb + 1):
        for j in range(-nb, nb + 1):
            ox = i * Wv[0] + j * Hv[0]
            oy = i * Wv[1] + j * Hv[1]
            for c in cells:
                for p in c['v']:
                    key = (round(p[0] + ox, 5), round(p[1] + oy, 5))
                    verts[key] = verts.get(key, 0) + 1
    return verts, Wv, Hv, cells

def block_span(cells):
    xs = [p[0] for c in cells for p in c['v']]
    ys = [p[1] for c in cells for p in c['v']]
    return min(xs), max(xs), min(ys), max(ys)

VAL_COLORS = {2: '#7f7f7f', 3: '#9467bd', 4: '#d62728', 5: '#8c564b',
              6: '#2ca02c', 8: '#ff7f0e', 12: '#1f77b4'}
PALETTE = ['#8dd3c7', '#ffffb3', '#bebada', '#fb8072', '#80b1d3', '#fdb462',
           '#b3de69', '#fccde5', '#d9d9d9', '#bc80bd', '#ccebc5', '#ffed6f',
           '#a6cee3', '#1f78b4', '#b2df8a', '#33a02c', '#fb9a99', '#e31a1c',
           '#fdbf6f', '#ff7f00', '#cab2d6', '#6a3d9a', '#ffff99', '#b15928']

def draw_block(ax, spec, title, ring=RING, annotate=True, labels=True):
    W = spec['W']; H = spec['H']
    Wv = (W[0], W[1]); Hv = (H[0], H[1])
    cells = spec['cells']
    xmin, xmax, ymin, ymax = block_span(cells)

    # 参照: 中央块格用彩色+下标; 邻居圈用浅灰 (非 (0,0) 偏移)
    for i in range(-ring, ring + 1):
        for j in range(-ring, ring + 1):
            ox = i * Wv[0] + j * Hv[0]
            oy = i * Wv[1] + j * Hv[1]
            central = (i == 0 and j == 0)
            for k, c in enumerate(cells):
                v = [(p[0] + ox, p[1] + oy) for p in c['v']]
                fc = PALETTE[k % len(PALETTE)] if central else '#e6e6e6'
                alpha = 0.85 if central else 0.35
                ax.add_patch(MPoly(v, closed=True, facecolor=fc, edgecolor='k',
                                   lw=1.0 if central else 0.6, alpha=alpha, zorder=2))
                if central and labels:
                    cx = sum(p[0] for p in v) / len(v)
                    cy = sum(p[1] for p in v) / len(v)
                    ax.text(cx, cy, str(k), ha='center', va='center', fontsize=8,
                            color='black', fontweight='bold', zorder=4)
    # 中央块轮廓
    ax.add_patch(MPoly([(0, 0), (Wv[0], Wv[1]), (Wv[0] + Hv[0], Wv[1] + Hv[1]),
                        (Hv[0], Hv[1])], closed=True, fill=False, edgecolor='red',
                       lw=1.5, zorder=7))

    if annotate:
        verts, _, _, _ = valency_map(spec)
        # 只在中央块真实跨距内的顶点画 valency 点
        m = 0.02
        drawn = {}
        for (px, py), n in verts.items():
            if xmin - m <= px <= xmax + m and ymin - m <= py <= ymax + m:
                drawn[(px, py)] = n
        for (px, py), n in sorted(drawn.items()):
            col = VAL_COLORS.get(n, '#000000')
            ax.plot(px, py, marker='o', ms=6, color=col, zorder=6)
            ax.annotate(str(n), (px, py), textcoords='offset points',
                        xytext=(4, 4), fontsize=7, color=col, zorder=7)

    pad = 0.25
    ax.set_xlim(xmin - pad, xmax + pad)
    ax.set_ylim(ymin - pad, ymax + pad)
    ax.set_aspect('equal')
    ax.grid(True, alpha=0.15, zorder=1)
    ax.set_title(f'{title}  block ({Wv[0]:.4f}x{Wv[1]:.4f}, {len(cells)} cells)')
    ax.set_xlabel('x'); ax.set_ylabel('y')

def show(name, out=None, labels=True):
    spec = load_all().get(name)
    if not spec:
        print('no spec for', name)
        return None
    out = out or os.path.join(DEF_DIR, f'block_{name}.png')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    fig, ax = plt.subplots(figsize=(10, 10))
    draw_block(ax, spec, title=name, ring=RING, annotate=True, labels=labels)
    fig.savefig(out, dpi=110, bbox_inches='tight')
    plt.close(fig)
    print('saved', out)
    return out

def menu(all_specs):
    names = sorted(all_specs.keys())
    print('可用的密铺:')
    for i, n in enumerate(names):
        print(f'  [{i}] {n}')
    sel = input('选择编号或输入密铺名 (回车=全部): ').strip()
    if sel == '':
        return names
    try:
        idx = int(sel)
        return [names[idx]]
    except ValueError:
        return [sel] if sel in all_specs else names

def main(argv):
    all_specs = load_all()
    if '-h' in argv or '--help' in argv:
        print(__doc__)
        return 0
    if '--list' in argv:
        for n in sorted(all_specs.keys()):
            print(n)
        return 0
    odir = DEF_DIR
    if '-o' in argv:
        odir = argv[argv.index('-o') + 1]
    if '--all' in argv or '-a' in argv:
        os.makedirs(odir, exist_ok=True)
        for n in sorted(all_specs.keys()):
            show(n, os.path.join(odir, f'block_{n}.png'))
        return 0
    args = [a for a in argv if a and not a.startswith('-')]
    targets = [a for a in args if a in all_specs]
    if not targets:
        targets = menu(all_specs)
    for n in targets:
        show(n, os.path.join(odir, f'block_{n}.png'))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]) or 0)
