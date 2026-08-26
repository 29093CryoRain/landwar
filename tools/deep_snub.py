#!/usr/bin/env python3
"""Deep analysis: for the 3 crooked snub tilings, determine
 - the translation lattice (primitive + appendix period),
 - a set of candidate rotations satisfying the direction spec,
 - whether an axis-aligned period exists (integer combos) for those rotations."""
import json, math, os, itertools
from fractions import Fraction

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'rubbish', '扭棱密铺构造解决方案_附录.json')
TOL = 1e-4


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def load(name):
    return json.load(open(APP, encoding='utf-8'))[name]


def adjacency(cells, wx, hx, hy, nbr=3):
    """For each directed edge, find (nb_cell_idx, dr, dc) of the reverse-matching edge."""
    B = len(cells)
    adj = {}
    for b, c in enumerate(cells):
        v = c['v']
        for k in range(c['n']):
            A = v[k]
            Bv = v[(k + 1) % c['n']]
            got = None
            for dr in range(-nbr, nbr + 1):
                if got:
                    break
                for dc in range(-nbr, nbr + 1):
                    ox = dc * wx + dr * hx
                    oy = dr * hy
                    for nb, ocell in enumerate(cells):
                        if got:
                            break
                        ov = ocell['v']
                        for kk in range(ocell['n']):
                            n0 = (ov[kk][0] + ox, ov[kk][1] + oy)
                            n1 = (ov[(kk + 1) % ocell['n']][0] + ox, ov[(kk + 1) % ocell['n']][1] + oy)
                            if dist(n0, Bv) < TOL and dist(n1, A) < TOL:
                                got = (nb, dr, dc, kk)
                                break
            adj[(b, k)] = got
    return adj


def cell_edges(c):
    v = c['v']
    out = []
    for k in range(c['n']):
        A = v[k]
        Bv = v[(k + 1) % c['n']]
        dx, dy = Bv[0] - A[0], Bv[1] - A[1]
        ln = math.hypot(dx, dy)
        ang = math.degrees(math.atan2(dy, dx)) % 180.0
        out.append((ln, ang))
    return out


def base_angle(cells):
    """The common base edge angle-ish set (mod 60 for tri/hex-ish tilings)."""
    angs = set()
    lens = {}
    for c in cells:
        for ln, ang in cell_edges(c):
            key = round(ln, 3)
            found = None
            for kk in lens:
                if abs(kk - key) < 0.03:
                    found = kk
                    break
            lens.setdefault(found if found is not None else key, []).append((ln, ang))
    result = {}
    for ln0, ar in sorted(lens.items()):
        angs = sorted(set(round(a1, 3) for _, a1 in ar))
        result[ln0] = angs
    return result


def main():
    for name in ['arch_33434', 'arch_33336', 'laves_33336']:
        sp = load(name)
        wx, hx, hy = sp['W'][0], sp['H'][0], sp['H'][1]
        cells = sp['cells']
        print('=' * 70)
        print(name, 'appendix W=(%.5f,%.5f) H=(%.5f,%.5f) B=%d' % (wx, sp['W'][1], hx, hy, len(cells)))
        es = base_angle(cells)
        for ln0, angs in sorted(es.items()):
            print('   len %.5f angles=%s' % (ln0, angs))
        # Number of distinct edge direction angles (undirected)
        allangs = set()
        for c in cells:
            for ln, ang in cell_edges(c):
                allangs.add(round(ang, 3))
        print('   all undirected edge angles (append): %s' % sorted(allangs))
        print('   appendix period area = %.5f  (B should = area if each cell area 1)' % (wx * hy))


if __name__ == '__main__':
    main()
