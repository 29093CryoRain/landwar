#!/usr/bin/env python3
"""gen_snub.py — rebuild the 3 crooked snub tilings at the direction-spec orientation.

Method (mirrors the laves_33434 fix):
  1. Load the appendix (raw construction) cells + period W,H.
  2. Rotate cells+period by theta (chosen so direction spec holds).
  3. Find integer combos m*W' + n*H' that are horizontal and vertical -> new axis-aligned period.
  4. Replicate cells over a patch and fold into [0,wx)x[0,hy), dedupe -> base cells (B).
  5. Validate: adjacency, cell area, vertex valency.
Outputs spec JSON for the requested names."""
import json, math, os, itertools

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'rubbish', '扭棱密铺构造解决方案_附录.json')
TOL = 1e-4
DEDUP_TOL = 1e-5  # center dedup tolerance (cell area ~1)


def poly_area(v):
    s = 0.0
    n = len(v)
    for i in range(n):
        a, b = v[i], v[(i + 1) % n]
        s += a[0] * b[1] - b[0] * a[1]
    return abs(s) * 0.5


def load_app(name):
    return json.load(open(APP, encoding='utf-8'))[name]


def rotv(x, y, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return (x * c - y * s, x * s + y * c)


def rotate_spec(sp, deg):
    """Rotate all cells + W,H by deg. Returns (cells, W, H)."""
    cells = []
    for c in sp['cells']:
        nv = []
        for p in c['v']:
            x, y = rotv(p[0], p[1], deg)
            nv.append([x, y])
        cx, cy = rotv(c['cx'], c['cy'], deg)
        cells.append({'n': c['n'], 'cx': cx, 'cy': cy, 'v': nv})
    W = rotv(sp['W'][0], sp['W'][1], deg)
    H = rotv(sp['H'][0], sp['H'][1], deg)
    return cells, W, H


def axis_period(W, H, deg, limit=40):
    """W,H are rotated period vectors (already rotated). Find integer combos giving axis-aligned."""
    u, v = W, H
    hs, vs = [], []
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
            area = abs((hm * u[0] + hn * v[0]) * (vm * u[1] + vn * v[1]) -
                       (hm * u[1] + hn * v[1]) * (vm * u[0] + vn * v[0]))
            combo = max(abs(hm), abs(hn), abs(vm), abs(vn))
            cand = (area, hlen, vlen, (hm, hn), (vm, vn), combo)
            if best is None or area < best[0] - 1e-6:
                best = cand
    return best


def fold_cells(cells, W, H, newW, newH, ring=4):
    """Replicate cells over a ring of (i*W+j*H) offsets, fold into [0,newW)x[0,newH), dedupe.
    newW=(wx,0), newH=(0,hy). Returns base cells (deduped by center)."""
    wx, hy = newW[0], newH[1]
    seen = []  # (u,v) normalized
    out = []
    for i in range(-ring, ring + 1):
        for j in range(-ring, ring + 1):
            ox = i * W[0] + j * H[0]
            oy = i * W[1] + j * H[1]
            for c in cells:
                cx, cy = c['cx'] + ox, c['cy'] + oy
                u = (cx / wx) % 1.0
                v = (cy / hy) % 1.0
                # numeric snap near 0/1
                if u < 1e-5 or u > 1 - 1e-5:
                    u = 0.0
                if v < 1e-5 or v > 1 - 1e-5:
                    v = 0.0
                dup = False
                for su, sv in seen:
                    if abs(su - u) < DEDUP_TOL and abs(sv - v) < DEDUP_TOL:
                        dup = True
                        break
                if dup:
                    continue
                # translate cell so (cx,cy) -> (u*wx, v*hy)
                dx, dy = u * wx - cx, v * hy - cy
                nv = []
                for p in c['v']:
                    nv.append([p[0] + ox + dx, p[1] + oy + dy])
                seen.append((u, v))
                out.append({'n': c['n'], 'cx': u * wx, 'cy': v * hy, 'v': nv})
    return out


def adjacency(cells, wx, hx, hy, nbr=3):
    B = len(cells)
    ok = 0
    tot = 0
    miss = []
    for b, c in enumerate(cells):
        v = c['v']
        for k in range(c['n']):
            A, Bv = v[k], v[(k + 1) % c['n']]
            found = False
            for dr in range(-nbr, nbr + 1):
                if found:
                    break
                for dc in range(-nbr, nbr + 1):
                    ox = dc * wx + dr * hx
                    oy = dr * hy
                    for nb, ocell in enumerate(cells):
                        if found:
                            break
                        ov = ocell['v']
                        for kk in range(ocell['n']):
                            n0 = (ov[kk][0] + ox, ov[kk][1] + oy)
                            n1 = (ov[(kk + 1) % ocell['n']][0] + ox, ov[(kk + 1) % ocell['n']][1] + oy)
                            if math.hypot(n0[0] - Bv[0], n0[1] - Bv[1]) < TOL and \
                               math.hypot(n1[0] - A[0], n1[1] - A[1]) < TOL:
                                found = True
                                break
            tot += 1
            if found:
                ok += 1
            else:
                miss.append((b, k))
    return ok, tot, miss


def edge_angle_set(cells):
    s = set()
    for c in cells:
        v = c['v']
        for k in range(c['n']):
            A, Bv = v[k], v[(k + 1) % c['n']]
            ang = math.degrees(math.atan2(Bv[1] - A[1], Bv[0] - A[0])) % 180.0
            s.add(round(ang, 1))
    return sorted(s)


def build(name, theta, want_axis=True, ring=5, limit=60):
    sp = load_app(name)
    cells, W, H = rotate_spec(sp, theta)
    # validate rotated is still a valid tiling under rotated W,H
    ok, tot, miss = adjacency(cells, W[0], H[0], H[1])
    if ok != tot:
        print('  !! rotated tiling invalid adj %d/%d %s' % (ok, tot, miss))
    res = axis_period(W, H, theta, limit)
    if not res:
        print('  !! no axis period at theta=%s' % theta)
        return None
    area, hlen, vlen, comboH, comboV, combo = res
    newW = (abs(comboH[0] * W[0] + comboH[1] * H[0]), 0.0)
    newH = (0.0, abs(comboV[0] * W[1] + comboV[1] * H[1]))
    base = fold_cells(cells, W, H, newW, newH, ring=ring)
    ea = edge_angle_set(base)
    areas = [poly_area(c['v']) for c in base]
    return {'W': [round(newW[0], 12), 0.0], 'H': [0.0, round(newH[1], 12)],
            'cells': base, 'area': area, 'B': len(base), 'combo': (comboH, comboV),
            'edge_angles': ea, 'cell_mass': min(areas), 'max': max(areas),
            'adj': adjacency(base, newW[0], 0.0, newH[1])}


def main():
    tests = [
        ('arch_33434', -45.0),
        ('arch_33434', 45.0),
        ('arch_33434', 0.0),
        ('arch_33336', -10.893),
        ('arch_33336', 30.0),
        ('laves_33336', -10.893),
        ('laves_33336', -30.0),
    ]
    for name, th in tests:
        print('=' * 70)
        print('build %s theta=%.3f' % (name, th))
        b = build(name, th)
        if b is None:
            continue
        print('  B=%d  axis W=%s H=%s  area=%.4f' % (b['B'], b['W'], b['H'], b['area']))
        print('  edge_angles=%s  cell area min/max=%.4f/%.4f' % (b['edge_angles'], b['cell_mass'], b['max']))
        print('  adjacency %d/%d missing=%s' % (b['adj'][0], b['adj'][1], b['adj'][2][:6]))


if __name__ == '__main__':
    main()
