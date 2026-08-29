#!/usr/bin/env python3
"""Render a folded spec JSON (or rotated candidate) to inspect direction vs the spec."""
import json, math, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MPoly

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'rubbish', '扭棱密铺构造解决方案_附录.json')
sys.path.insert(0, ROOT)
import tools.gen_snub as g


def draw(cells, W, H, title, out, ring=2):
    fig, ax = plt.subplots(figsize=(10, 10))
    wx, hy = W[0], H[1]
    for i in range(-ring, ring + 1):
        for j in range(-ring, ring + 1):
            ox = i * W[0] + j * H[0]
            oy = i * W[1] + j * H[1]
            central = (i == 0 and j == 0)
            for kk, c in enumerate(cells):
                v = [(p[0] + ox, p[1] + oy) for p in c['v']]
                fc = '#8dd3c7' if central else '#dddddd'
                alpha = 0.9 if central else 0.35
                ax.add_patch(MPoly(v, closed=True, facecolor=fc, edgecolor='k',
                                   lw=1.0 if central else 0.6, alpha=alpha))
                if central:
                    cx = sum(p[0] for p in v) / len(v)
                    cy = sum(p[1] for p in v) / len(v)
                    ax.text(cx, cy, str(kk), ha='center', va='center', fontsize=8,
                            color='black', fontweight='bold')
    ax.add_patch(MPoly([(0, 0), (wx, 0), (wx + H[0], hy), (H[0], hy)], closed=True,
                       fill=False, edgecolor='red', lw=1.8))
    ax.set_aspect('equal'); ax.grid(True, alpha=0.15)
    xmin = min(min(p[0] for p in c['v']) for c in cells) + (-ring) * W[0]
    xmax = max(max(p[0] for p in c['v']) for c in cells) + ring * W[0]
    ymin = min(min(p[1] for p in c['v']) for c in cells) + (-ring) * H[1]
    ymax = max(max(p[1] for p in c['v']) for c in cells) + ring * H[1]
    ax.set_xlim(xmin - 0.5, xmax + 0.5); ax.set_ylim(ymin - 0.5, ymax + 0.5)
    ax.set_title('%s  (%s x %s, B=%d)' % (title, round(wx, 3), round(hy, 3), len(cells)))
    ax.set_xlabel('x'); ax.set_ylabel('y')
    fig.savefig(out, dpi=110, bbox_inches='tight')
    plt.close(fig)
    print('saved', out)


def main():
    # arch_33434 at theta=0 (appendix) and theta=-45
    for name, th in [('arch_33434', 0.0), ('arch_33434', -45.0), ('arch_33434', 45.0)]:
        sp = g.load_app(name)
        cells, W, H = g.rotate_spec(sp, th)
        # fold into the natural period for that theta (axis period if available)
        res = g.axis_period(W, H, th, 60)
        if res:
            area, hl, vl, ch, cv, cb = res
            nW = (abs(ch[0] * W[0] + ch[1] * H[0]), 0.0)
            nH = (0.0, abs(cv[0] * W[1] + cv[1] * H[1]))
            base = g.fold_cells(cells, W, H, nW, nH, ring=5)
        else:
            base, nW, nH = cells, W, H
        draw(base, nW, nH, '%s theta=%s' % (name, th),
             os.path.join(ROOT, 'tools/blocks', 'cand_%s_t%s.png' % (name, str(th).replace('.', '_'))))
        print('   B=%d W=%s H=%s edge=%s' % (len(base), nW, nH, g.edge_angle_set(base)))


if __name__ == '__main__':
    main()
