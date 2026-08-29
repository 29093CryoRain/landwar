#!/usr/bin/env python3
"""dual_check.py — compute the Laves dual of an Arch tiling spec (for arch_33434 transpose)
and compare to the known-good current laves_33434. Settles which chirality is 'correct'."""
import json, math, os, sys
from collections import defaultdict
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g
import tools.build_fixed as bf

POLY_TOL = 1e-3


def dual_faces(cells, wx, wy, hx, hy, ring=3):
    """For each vertex of the tiling, form a face from incident cell centers (ordered around).
    Returns list of polygons (list of (x,y) points)."""
    # collect all (replicated) cells with absolute vertices; group vertices by rounded coord
    vert_cells = defaultdict(set)  # rounded vertex -> set of cell-center tuples (as (cx,cy))
    cell_centers = []
    for i in range(-ring, ring + 1):
        for j in range(-ring, ring + 1):
            ox = i * wx + j * hx
            oy = i * wy + j * hy
            for c in cells:
                cc = (c['cx'] + ox, c['cy'] + oy)
                cell_centers.append(cc)
                for p in c['v']:
                    key = (round(p[0] + ox, 4), round(p[1] + oy, 4))
                    vert_cells[key].add(cc)
    faces = []
    for key, centers in vert_cells.items():
        if len(centers) < 3:
            continue
        # order centers around the vertex by angle
        cx, cy = key
        ordered = sorted(centers, key=lambda c: math.atan2(c[1] - cy, c[0] - cx))
        faces.append((key, [ (c[0], c[1]) for c in ordered ]))
    return faces


def poly_area(v):
    s = 0.0
    n = len(v)
    for i in range(n):
        a, b = v[i], v[(i + 1) % n]
        s += a[0] * b[1] - b[0] * a[1]
    return abs(s) * 0.5


def main():
    sp = g.load_app('arch_33434')
    r, nW, nH = bf.build('arch_33434', 90, True, True)
    cells = r['cells']
    wx, hx, hy = r['nW'][0], 0.0, r['nH'][1]
    faces = dual_faces(cells, wx, 0.0, hx, hy, ring=2)
    pent = [f for f in faces if len(f[1]) == 5]
    print('transpose arch_33434 dual: %d vertices, %d pentagons' % (len(faces), len(pent)))
    # scale: laves faces area should ==1; arch faces avg area 1 but cell types vary.
    # The dual pentagon area ~ related to arch cell areas. Just print a few signed areas.
    for pnt in pent[:6]:
        vp = pnt[1]
        sa = 0.0
        n = len(vp)
        for i in range(n):
            a, b = vp[i], vp[(i + 1) % n]
            sa += a[0] * b[1] - b[0] * a[1]
        print('   pent at %s  signed=%.4f  nverts=%d' % (pnt[0], sa / 2.0, n))

    # append: compute for appendix (no mirror) too for comparison of chirality sign
    r2, nW2, nH2 = bf.build('arch_33434', 0, False, True)
    cells2 = r2['cells']
    faces2 = dual_faces(cells2, r2['nW'][0], 0.0, 0.0, r2['nH'][1], ring=2)
    pent2 = [f for f in faces2 if len(f[1]) == 5]
    print('appendix arch_33434 dual: %d pentagons' % len(pent2))
    for pnt in pent2[:6]:
        vp = pnt[1]
        sa = 0.0
        n = len(vp)
        for i in range(n):
            a, b = vp[i], vp[(i + 1) % n]
            sa += a[0] * b[1] - b[0] * a[1]
        print('   pent at %s  signed=%.4f' % (pnt[0], sa / 2.0))


if __name__ == '__main__':
    main()
