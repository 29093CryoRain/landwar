#!/usr/bin/env python3
"""render_final.py — render the corrected data as actually stored in the spec files."""
import json, math, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MPoly

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def render(name, which_file, out, ring=1):
    d = json.load(open(os.path.join(ROOT, which_file), encoding='utf-8'))[name]
    cells = d['cells']
    W = (d['W'][0], d['W'][1])
    H = (d['H'][0], d['H'][1])
    fig, ax = plt.subplots(figsize=(9, 9))
    for i in range(-ring, ring + 1):
        for j in range(-ring, ring + 1):
            ox = i * W[0] + j * H[0]
            oy = i * W[1] + j * H[1]
            central = (i == 0 and j == 0)
            for kk, c in enumerate(cells):
                v = [(p[0] + ox, p[1] + oy) for p in c['v']]
                fc = '#8dd3c7' if central else '#dddddd'
                ax.add_patch(MPoly(v, closed=True, facecolor=fc, edgecolor='k',
                                   lw=1.0 if central else 0.5, alpha=0.92 if central else 0.35))
                if central:
                    cx = sum(p[0] for p in v) / len(v); cy = sum(p[1] for p in v) / len(v)
                    ax.text(cx, cy, str(kk), ha='center', va='center', fontsize=7, fontweight='bold')
    ax.add_patch(MPoly([(0, 0), (W[0], W[1]), (W[0] + H[0], W[1] + H[1]), (H[0], H[1])],
                       fill=False, edgecolor='red', lw=1.8))
    ax.set_aspect('equal'); ax.grid(True, alpha=0.15)
    xs = [p[0] for c in cells for p in c['v']] + [0, W[0], W[0] + H[0], H[0]]
    ys = [p[1] for c in cells for p in c['v']] + [0, W[1], W[1] + H[1], H[1]]
    ax.set_xlim(min(xs) - 0.4, max(xs) + 0.4); ax.set_ylim(min(ys) - 0.4, max(ys) + 0.4)
    ax.set_title('%s  (W=(%.3f,%.3f) H=(%.3f,%.3f) B=%d)' % (name, W[0], W[1], H[0], H[1], len(cells)))
    fig.savefig(out, dpi=110, bbox_inches='tight')
    plt.close(fig)
    print('saved', out)


def main():
    d = os.path.join(ROOT, 'tools/blocks')
    render('arch_33434', 'data/tiling_specs_arch.json', os.path.join(d, 'final_arch_33434_render.png'))
    render('arch_33336', 'data/tiling_specs_arch.json', os.path.join(d, 'final_arch_33336_render.png'))
    render('laves_33434', 'data/tiling_specs_laves.json', os.path.join(d, 'final_laves_33434_render.png'))
    render('laves_33336', 'data/tiling_specs_laves.json', os.path.join(d, 'final_laves_33336_render.png'))


if __name__ == '__main__':
    main()
