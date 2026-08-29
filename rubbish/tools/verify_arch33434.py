#!/usr/bin/env python3
"""Verify + render arch_33434 chirality fix (transpose)."""
import json, math, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MPoly

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g
import tools.spec_search as ss
import tools.build_fixed as bf


def main():
    # spec predicate on raw appendix (should be FALSE), and on transform candidates
    sp = g.load_app('arch_33434')
    raw = sp['cells']
    wx, hx, hy = sp['W'][0], sp['H'][0], sp['H'][1]
    print('appendix spec satisfied?', ss.spec_arch33434(raw, wx, hx, hy))
    for deg, mir in [(0, True), (90, True), (270, True)]:
        cells = bf.transform_cells(sp['cells'], deg, mir)
        print('transform deg=%d mir=%s spec=%s' % (deg, mir, ss.spec_arch33434(cells, wx, hx, hy)))

    # build the transpose (deg=90,mir=True) version axis-aligned
    r, nW, nH = bf.build('arch_33434', 90, True, True)
    cells = r['cells']
    print('BUILT transpose B=%d spec=%s adj=%d/%d' % (r['B'], ss.spec_arch33434(cells, r['nW'][0], 0.0, r['nH'][1]), r['adj'][0], r['adj'][1]))
    # render
    fig, ax = plt.subplots(figsize=(10, 10))
    wx, hy, Hx = r['nW'][0], r['nH'][1], 0.0
    for i in range(-2, 3):
        for j in range(-2, 3):
            ox = i * wx + j * Hx
            oy = i * 0.0 + j * hy
            central = (i == 0 and j == 0)
            for kk, c in enumerate(cells):
                v = [(p[0] + ox, p[1] + oy) for p in c['v']]
                fc = '#8dd3c7' if central else '#dddddd'
                ax.add_patch(MPoly(v, closed=True, facecolor=fc, edgecolor='k', lw=1.0 if central else 0.5,
                                   alpha=0.9 if central else 0.35))
                if central:
                    cx = sum(p[0] for p in v) / len(v)
                    cy = sum(p[1] for p in v) / len(v)
                    ax.text(cx, cy, str(kk), ha='center', va='center', fontsize=8, fontweight='bold')
    ax.add_patch(MPoly([(0, 0), (wx, 0), (wx + Hx, hy), (Hx, hy)], fill=False, edgecolor='red', lw=1.8))
    xs = [p[0] for c in cells for p in c['v']]
    ys = [p[1] for c in cells for p in c['v']]
    ax.set_xlim(min(xs) - 1, max(xs) + 1); ax.set_ylim(min(ys) - 1, max(ys) + 1)
    ax.set_aspect('equal'); ax.grid(True, alpha=0.15)
    ax.set_title('arch_33434 transpose (B=%d)' % r['B'])
    plt.savefig(os.path.join(ROOT, 'tools/blocks', 'arch33434_transpose.png'), dpi=110, bbox_inches='tight')
    print('saved transpose render')


if __name__ == '__main__':
    main()
