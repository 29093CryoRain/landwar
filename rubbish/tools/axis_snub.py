#!/usr/bin/env python3
"""Corrected: search axis-aligned period as integer combos of ROTATED appendix period W,H.
Also determine hexagon/square orientation per direction spec, and report edge-angle sets
for candidate rotations so we can pick theta satisfying the direction spec."""
import json, math, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'rubbish', '扭棱密铺构造解决方案_附录.json')
TOL = 1e-4


def load_app(name):
    return json.load(open(APP, encoding='utf-8'))[name]


def rotv(vec, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return (vec[0] * c - vec[1] * s, vec[0] * s + vec[1] * c)


def axis_combo(u, v, limit=60):
    """u,v are (x,y). Find nonzero integer combos giving horizontal and vertical periods.
    Returns (hvec, vvec, combo_h, combo_v, B) where B = area/cell_area if cell_area=1."""
    hs = []
    vs = []
    for m in range(-limit, limit + 1):
        for n in range(-limit, limit + 1):
            if m == 0 and n == 0:
                continue
            x = m * u[0] + n * v[0]
            y = m * u[1] + n * v[1]
            if abs(y) < 1e-3 and abs(x) > 1e-3:
                hs.append((abs(x), m, n))
            if abs(x) < 1e-3 and abs(y) > 1e-3:
                vs.append((abs(y), m, n))
    if not hs or not vs:
        return None
    best = None
    for hlen, hm, hn in sorted(hs):
        for vlen, vm, vn in sorted(vs):
            hx, hy = hm * u[0] + hn * v[0], hm * u[1] + hn * v[1]
            vx, vy = vm * u[0] + vn * v[0], vm * u[1] + vn * v[1]
            area = abs(hx * vy - hy * vx)
            combo = max(abs(hm), abs(hn), abs(vm), abs(vn))
            cand = (area, hlen, vlen, (hm, hn), (vm, vn), combo)
            if best is None or (area < best[0] - 1e-6) or \
               (abs(area - best[0]) < 1e-6 and combo < best[-1]):
                best = cand
    return best


def edge_angles(cells):
    s = set()
    for c in cells:
        v = c['v']
        for k in range(c['n']):
            A = v[k]; Bv = v[(k + 1) % c['n']]
            ang = math.degrees(math.atan2(Bv[1] - A[1], Bv[0] - A[0])) % 180.0
            s.add(round(ang, 2))
    return sorted(s)


def hex_edges(cells):
    for c in cells:
        if c['n'] == 6:
            angs = []
            v = c['v']
            for k in range(c['n']):
                A = v[k]; Bv = v[(k + 1) % c['n']]
                ang = math.degrees(math.atan2(Bv[1] - A[1], Bv[0] - A[0])) % 180.0
                angs.append(round(ang, 2))
            return (c['cx'], c['cy']), sorted(angs)
    return None


def sq_edges(cells):
    for c in cells:
        if c['n'] == 4:
            angs = []
            v = c['v']
            for k in range(c['n']):
                A = v[k]; Bv = v[(k + 1) % c['n']]
                ang = math.degrees(math.atan2(Bv[1] - A[1], Bv[0] - A[0])) % 180.0
                angs.append(round(ang, 2))
            return (c['cx'], c['cy']), sorted(angs)
    return None


def report(name):
    sp = load_app(name)
    wx, hx, hy = sp['W'][0], sp['H'][0], sp['H'][1]
    W = (wx, 0.0)
    H = (hx, hy)
    print('=' * 78)
    print('%s  appendix W=(%.5f,%.5f) H=(%.5f,%.5f) B=%d' % (name, wx, sp['W'][1], hx, hy, len(sp['cells'])))
    print('   edge angles  (appendix): %s' % edge_angles(sp['cells']))
    if name.startswith('arch_33336'):
        hx0, hangs = hex_edges(sp['cells'])
        print('   hexagon center (%.4f,%.4f) edge angles %s' % (hx0[0], hx0[1], hangs))
    if name.startswith('arch_33434'):
        sc, sangs = sq_edges(sp['cells'])
        print('   square   center (%.4f,%.4f) edge angles %s' % (sc[0], sc[1], sangs))
    print('   candidate theta -> axis-aligned period (area, width, height, comboH, comboV):')
    for th in range(-50, 51, 5):
        u = rotv(W, th)
        v = rotv(H, th)
        res = axis_combo(u, v)
        # edge angles after rotation
        ee = []
        for c in sp['cells']:
            for k in range(c['n']):
                A = c['v'][k]; Bv = c['v'][(k + 1) % c['n']]
                ang = math.degrees(math.atan2(Bv[1] - A[1], Bv[0] - A[0])) % 180.0
                ee.append(round((ang + th) % 180.0, 1))
        ee = sorted(set(ee))
        if res:
            print('   theta=%6.1f  area=%.4f  W=(%.4f x %.4f)B=%.1f  ee=%s' % (
                th, res[0], res[1], res[2], res[0], ee))
        else:
            print('   theta=%6.1f  NO-AXIS  ee=%s' % (th, ee))


def main():
    report('arch_33434')
    report('arch_33336')
    report('laves_33336')


if __name__ == '__main__':
    main()
