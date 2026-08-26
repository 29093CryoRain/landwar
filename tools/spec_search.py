#!/usr/bin/env python3
"""Search rotation + mirror transforms that satisfy each crooked tiling's direction-spec
predicate. Predicates encode 开发思路.txt 方向规范 for arch_33434 / arch_33336 / laves_33336."""
import json, math, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g

TOL = 1e-3


def rotp(p, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c)


def transform(cells, deg, mirror):
    out = []
    for c in cells:
        nv = []
        for p in c['v']:
            x, y = p[0], -p[1] if mirror else p[1]
            x, y = rotp((x, y), deg)
            nv.append([x, y])
        cx, cy = c['cx'], -c['cy'] if mirror else c['cy']
        cx, cy = rotp((cx, cy), deg)
        out.append({'n': c['n'], 'cx': cx, 'cy': cy, 'v': nv})
    return out


def tri_apex_deg(c):
    v = c['v']
    n = len(v)
    edges = sorted([(math.hypot(v[(k + 1) % n][0] - v[k][0], v[(k + 1) % n][1] - v[k][1]), k)
                    for k in range(n)], key=lambda x: -x[0])
    apex = (edges[0][1] + 2) % n
    ax, ay = v[apex]
    return math.degrees(math.atan2(ay, ax)) % 360.0


def neighbor_map(cells, wx, hx, hy, nbr=3):
    B = len(cells)
    adj = {}
    for b, c in enumerate(cells):
        v = c['v']
        for k in range(c['n']):
            A, Bv = v[k], v[(k + 1) % c['n']]
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
                            if math.hypot(n0[0] - Bv[0], n0[1] - Bv[1]) < TOL and \
                               math.hypot(n1[0] - A[0], n1[1] - A[1]) < TOL:
                                got = (nb, dr, dc, kk)
                                break
                adj[(b, k)] = got
    return adj


def hex_volt_angles(c):
    v = c['v']
    angs = []
    for k in range(c['n']):
        a, b = v[k], v[(k + 1) % c['n']]
        angs.append((math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])) % 360.0))
    return angs


def pent_edges(c):
    """list of (length, angle_deg_0_180) for a pentagon."""
    v = c['v']
    n = len(v)
    out = []
    for k in range(n):
        a, b = v[k], v[(k + 1) % n]
        out.append((math.hypot(b[0] - a[0], b[1] - a[1]),
                    math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])) % 180.0))
    return out


def is_horiz(t):
    return abs(t) < 5 or abs(t - 180) < 5


def is_vert(t):
    return abs(t - 90) < 5


def spec_arch33434(cells, wx, hx, hy):
    """some square whose bottom-left edge-neighbour is RIGHT triangle and bottom-right is UP."""
    adj = neighbor_map(cells, wx, hx, hy)
    for b, c in enumerate(cells):
        if c['n'] != 4:
            continue
        cx, cy = c['cx'], c['cy']
        for k in range(4):
            a, bb = c['v'][k], c['v'][(k + 1) % 4]
            mid = ((a[0] + bb[0]) / 2, (a[1] + bb[1]) / 2)
            dirdeg = math.degrees(math.atan2(mid[1] - cy, mid[0] - cx)) % 360.0
            nb = adj[(b, k)]
            if not nb or nb[0] is None or cells[nb[0]]['n'] != 3:
                continue
            apex = tri_apex_deg(cells[nb[0]])
            # bottom-right edge dir ~ 315 deg-45 deg window; bottom-left ~ 225 deg-window
            if 280 < dirdeg < 350:  # bottom-right
                # want UP triangle: apex near 90
                if abs(apex - 90) < 25 or abs(apex - 450) < 25:
                    pass
                else:
                    continue
            elif 190 < dirdeg < 260:  # bottom-left
                # want RIGHT triangle: apex near 0/360
                if apex < 25 or apex > 335:
                    pass
                else:
                    continue
            else:
                continue
            # both bottom edges present and satisfy; check the OTHER bottom edge too
            # For square with edge at 'dirdeg' bottom-right=up, find bottom-left edge = right
            okl, okr = False, False
            for k2 in range(4):
                a2, b2 = c['v'][k2], c['v'][(k2 + 1) % 4]
                mid2 = ((a2[0] + b2[0]) / 2, (a2[1] + b2[1]) / 2)
                d2 = math.degrees(math.atan2(mid2[1] - cy, mid2[0] - cx)) % 360.0
                nb2 = adj[(b, k2)]
                if not nb2 or nb2[0] is None or cells[nb2[0]]['n'] != 3:
                    continue
                a2p = tri_apex_deg(cells[nb2[0]])
                if 280 < d2 < 350 and (abs(a2p - 90) < 25 or abs(a2p - 450) < 25):
                    okr = True
                if 190 < d2 < 260 and (a2p < 25 or a2p > 335):
                    okl = True
            if okl and okr:
                return True
    return False


def spec_arch33336(cells):
    """some 左右六边形 (hexagon with a vertex at left(180) and right(0))."""
    for c in cells:
        if c['n'] != 6:
            continue
        v = c['v']
        vangs = [math.degrees(math.atan2(p[1], p[0])) % 360.0 for p in v]  # rel to cell center? no
        # vertex angle relative to centroid
        cx = sum(p[0] for p in v) / 6
        cy = sum(p[1] for p in v) / 6
        vangs = sorted(math.degrees(math.atan2(p[1] - cy, p[0] - cx)) % 360.0 for p in v)
        has_left = any(abs(a - 180) < 3 for a in vangs)
        has_right = any(abs(a) < 3 or abs(a - 360) < 3 for a in vangs)
        if has_left and has_right:
            return True
    return False


def spec_laves33336(cells):
    """pentagon with a horizontal long edge (b) and no vertical a/b edge."""
    for c in cells:
        if c['n'] != 5:
            continue
        es = pent_edges(c)
        lens = sorted(e[0] for e in es)
        b_len = lens[-1]  # longest
        has_horiz_long = any(is_horiz(e[1]) and abs(e[0] - b_len) < 1e-3 for e in es)
        if not has_horiz_long:
            continue
        if any(is_vert(e[1]) for e in es):
            continue
        return True
    return False


def main():
    cases = [
        ('arch_33434', spec_arch33434, True),
        ('arch_33336', spec_arch33336, True),
        ('laves_33336', spec_laves33336, True),
    ]
    for name, pred, need_pred in cases:
        sp = g.load_app(name)
        wx, hx, hy = sp['W'][0], sp['H'][0], sp['H'][1]
        print('=' * 72)
        print(name)
        found = []
        for mirror in (False, True):
            for deg in range(0, 360, 15):
                cells = transform(sp['cells'], deg, mirror)
                if pred(cells, wx, hx, hy) if name == 'arch_33434' else pred(cells):
                    found.append((deg, mirror))
        # finer search around found for the 33434
        print('  rotation+mirror satisfying spec (15deg grid):')
        for f in found:
            print('    theta=%4d mirror=%s' % (f[0], f[1]))


if __name__ == '__main__':
    main()
