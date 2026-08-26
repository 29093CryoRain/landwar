#!/usr/bin/env python3
"""Compute primitive translation lattice of each snub tiling (from appendix base cells),
verify cell areas, and ground-truth the laves_33434 fix. Then for each crooked tiling test
candidate direction-spec rotations for axis-aligned period existence."""
import json, math, os
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'rubbish', '扭棱密铺构造解决方案_附录.json')
CUR_L = os.path.join(ROOT, 'data', 'tiling_specs_laves.json')
TOL = 1e-4


def poly_area(v):
    s = 0.0
    n = len(v)
    for i in range(n):
        a = v[i]
        b = v[(i + 1) % n]
        s += a[0] * b[1] - b[0] * a[1]
    return abs(s) * 0.5


def center(c):
    return (c['cx'], c['cy'])


def load_app(name):
    return json.load(open(APP, encoding='utf-8'))[name]


def load_cur(name):
    d = json.load(open(CUR_L, encoding='utf-8'))
    return d.get(name)


def primitive_lattice(cells):
    """Given base cells (each is a lattice orbit representative), return the two shortest
    linearly independent lattice vectors as integer combos in terms of appendix W,H is NOT
    needed; instead return them as absolute (x,y) vectors obtained from center differences."""
    cen = [center(c) for c in cells]
    B = len(cen)
    # All pairwise differences among base centers are lattice vectors.
    diffs = []
    for i in range(B):
        for j in range(B):
            if i == j:
                continue
            dx = cen[i][0] - cen[j][0]
            dy = cen[i][1] - cen[j][1]
            if math.hypot(dx, dy) > 1e-6:
                diffs.append((dx, dy))
    diffs.sort(key=lambda p: math.hypot(p[0], p[1]))
    # Reduced basis: pick shortest vector, then shortest not parallel (independent).
    u = None
    for d in diffs:
        if u is None:
            u = d
            continue
        # independent of u? cross != 0
        cross = u[0] * d[1] - u[1] * d[0]
        if abs(cross) > 1e-4:
            v = d
            break
    return u, v


def rot(vec, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return (vec[0] * c - vec[1] * s, vec[0] * s + vec[1] * c)


def axis_period_combo(u, v, limit=20):
    """Find integer combos m*u+n*v horizontal and k*u+l*v vertical (nonzero), with |m|,|n|<=limit.
    Returns (hvec, vvec, m,n,k,l) or None. hvec len and vvec len reported."""
    best = None
    for limit2 in range(1, limit + 1):
        cands_h = []
        cands_v = []
        for m in range(-limit2, limit2 + 1):
            for n in range(-limit2, limit2 + 1):
                if m == 0 and n == 0:
                    continue
                x = m * u[0] + n * v[0]
                y = m * u[1] + n * v[1]
                if abs(y) < 1e-4:  # horizontal
                    cands_h.append((abs(x), m, n))
                if abs(x) < 1e-4:  # vertical
                    cands_v.append((abs(y), m, n))
        for hlen, hm, hn in sorted(cands_h):
            for vlen, vm, vn in sorted(cands_v):
                # determine area (cross of the two chosen vectors)
                hx = hm * u[0] + hn * v[0]
                hy = 0.0
                vx = 0.0
                vy = vm * u[1] + vn * v[1]
                area = abs(hx * vy - hy * vx)
                return (hx, 0.0), (0.0, vy), (hm, hn), (vm, vn), area
    return None


def main():
    print('verify cell areas (appendix):')
    for name in ['arch_33434', 'arch_33336', 'laves_33336']:
        cells = load_app(name)['cells']
        areas = [poly_area(c['v']) for c in cells]
        print('  %-12s areas=%s  min/max=%.5f/%.5f' % (name,
              ['%.4f' % a for a in areas[:4]], min(areas), max(areas)))
    print()
    print('laves_33434 ground truth: appendix vs current(fixed):')
    ap = load_app('laves_33434')
    cur = load_cur('laves_33434')
    for label, s in [('appendix', ap), ('current', cur)]:
        cells = s['cells']
        wx, hx, hy = s['W'][0], s['H'][0], s['H'][1]
        areas = [poly_area(c['v']) for c in cells]
        u, v = primitive_lattice(cells)
        print('  %-9s W=(%.5f,%.5f) H=(%.5f,%.5f) B=%d  area=%s min/max %.5f/%.5f  primitive u=(%.5f,%.5f) v=(%.5f,%.5f)'
              % (label, wx, s['W'][1], hx, hy, len(cells),
                 ['%.4f' % a for a in areas[:3]], min(areas), max(areas), u[0], u[1], v[0], v[1]))
        # edge directions
        allang = set()
        for c in cells:
            vv = c['v']
            for k in range(c['n']):
                A = vv[k]; Bv = vv[(k + 1) % c['n']]
                ang = math.degrees(math.atan2(Bv[1] - A[1], Bv[0] - A[0])) % 180.0
                allang.add(round(ang, 3))
        print('           edge angles=%s' % sorted(allang))
        res = axis_period_combo(u, v, 30)
        print('           axis-aligned period from primitive: %s' % (res,))
    print()
    print('crooked tilings: primitive + axis-aligned period search at candidate rotations:')
    for name, thetas in [('arch_33434', [-15, '+15', 45, -45]),
                         ('arch_33336', [-40.893, -10.893, 49.107]),
                         ('laves_33336', [-10.893, -40.893, 49.107])]:
        cells = load_app(name)['cells']
        u, v = primitive_lattice(cells)
        print('== %s  primitive u=(%.5f,%.5f) v=(%.5f,%.5f)' % (name, u[0], u[1], v[0], v[1]))
        for t in thetas:
            if isinstance(t, str):
                tv = float(t.strip('+'))
            else:
                tv = t
            ur = rot(u, tv)
            vr = rot(v, tv)
            res = axis_period_combo(ur, vr, 40)
            print('   theta=%7s -> axis-aligned period: %s' % (t, res))


if __name__ == '__main__':
    main()
