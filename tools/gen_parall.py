#!/usr/bin/env python3
"""gen_parall.py — build parallelogram (W.y!=0, H.x!=0) spec for arch_33336 / laves_33336 at
the direction-spec orientation. Just rotates the appendix cells+period (rotation preserves the
tiling & B). Renders both chirality variants for visual verification."""
import json, math, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MPoly

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g
import tools.spec_search as ss


def rotate_spec_and_period(sp, deg, mirror):
    cells = ss.transform(  # transform from spec_search: (cells,deg,mirror)
        sp['cells'], deg, mirror)
    # period: mirror about x-axis then rotate
    W = (sp['W'][0], sp['W'][1])
    H = (sp['H'][0], sp['H'][1])
    Wm = (W[0], -W[1] if mirror else W[1])
    Hm = (H[0], -H[1] if mirror else H[1])
    Wr = ss.rotp(Wm, deg)
    Hr = ss.rotp(Hm, deg)
    return cells, Wr, Hr


def render(cells, W, H, title, out):
    fig, ax = plt.subplots(figsize=(10, 10))
    ring = 1
    for i in range(-ring, ring + 1):
        for j in range(-ring, ring + 1):
            ox = i * W[0] + j * H[0]
            oy = i * W[1] + j * H[1]
            central = (i == 0 and j == 0)
            for kk, c in enumerate(cells):
                v = [(p[0] + ox, p[1] + oy) for p in c['v']]
                fc = '#8dd3c7' if central else '#dddddd'
                ax.add_patch(MPoly(v, closed=True, facecolor=fc, edgecolor='k',
                                   lw=1.0 if central else 0.5, alpha=0.9 if central else 0.35))
                if central:
                    cx = sum(p[0] for p in v) / len(v); cy = sum(p[1] for p in v) / len(v)
                    ax.text(cx, cy, str(kk), ha='center', va='center', fontsize=7, fontweight='bold')
    ax.add_patch(MPoly([(0, 0), (W[0], W[1]), (W[0] + H[0], W[1] + H[1]), (H[0], H[1])],
                       fill=False, edgecolor='red', lw=1.8))
    ax.set_aspect('equal'); ax.grid(True, alpha=0.15)
    xs = [p[0] for c in cells for p in c['v']] + [0, W[0], W[0] + H[0], H[0]]
    ys = [p[1] for c in cells for p in c['v']] + [0, W[1], W[1] + H[1], H[1]]
    ax.set_xlim(min(xs) - 0.5, max(xs) + 0.5); ax.set_ylim(min(ys) - 0.5, max(ys) + 0.5)
    ax.set_title(title)
    fig.savefig(out, dpi=110, bbox_inches='tight')
    plt.close(fig)
    print('saved', out)


def main():
    # exact spec theta (mod 60) from append edge base angle; compute from hexagon for arch,
    # from pentagon long edge for laves.
    name = 'arch_33336'
    sp = g.load_app(name)
    for (deg, mir, tag) in [(-10.893, False, 'nomirror'), (-10.893, True, 'mirror')]:
        cells, W, H = rotate_spec_and_period(sp, deg, mir)
        # validate adjacency at rotated period
        ok, tot, miss = g.adjacency(cells, W[0], H[0], H[1])
        print('arch_33336 deg=%.3f mir=%s  area=%.4f B=%d adj=%d/%d miss=%s' % (
            deg, mir, abs(W[0] * H[1] - W[1] * H[0]), len(cells), ok, tot, miss[:4]))
        render(cells, W, H, 'arch_33336 theta=%.3f %s' % (deg, tag),
               os.path.join(ROOT, 'tools/blocks', 'par_arch33336_%s.png' % tag))
    name = 'laves_33336'
    sp = g.load_app(name)
    for (deg, mir, tag) in [(-10.893, False, 'nomirror'), (-10.893, True, 'mirror')]:
        cells, W, H = rotate_spec_and_period(sp, deg, mir)
        ok, tot, miss = g.adjacency(cells, W[0], H[0], H[1])
        print('laves_33336 deg=%.3f mir=%s  area=%.4f B=%d adj=%d/%d miss=%s' % (
            deg, mir, abs(W[0] * H[1] - W[1] * H[0]), len(cells), ok, tot, miss[:4]))
        render(cells, W, H, 'laves_33336 theta=%.3f %s' % (deg, tag),
               os.path.join(ROOT, 'tools/blocks', 'par_laves33336_%s.png' % tag))


if __name__ == '__main__':
    main()
