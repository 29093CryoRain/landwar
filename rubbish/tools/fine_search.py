#!/usr/bin/env python3
"""Fine search: exact direction-spec angle/chirality for 33336 tilings, and determine the
smallest period (allowing W.y) at that orientation. Reports B and whether W is non-horizontal.
"""
import json, math, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g
import tools.spec_search as ss


def transform_cells(cells, deg, mirror):
    out = []
    for c in cells:
        nv = []
        for p in c['v']:
            x, y = p[0], -p[1] if mirror else p[1]
            x, y = ss.rotp((x, y), deg)
            nv.append([x, y])
        cx, cy = c['cx'], -c['cy'] if mirror else c['cy']
        cx, cy = ss.rotp((cx, cy), deg)
        out.append({'n': c['n'], 'cx': cx, 'cy': cy, 'v': nv})
    return out


def is_horiz(t, tol=0.5):
    return abs(t) < tol or abs(t - 180) < tol


def is_vert(t, tol=0.5):
    return abs(t - 90) < tol


def spec_laves_tight(cells):
    for c in cells:
        if c['n'] != 5:
            continue
        es = ss.pent_edges(c)
        lens = sorted(e[0] for e in es)
        b_len = lens[-1]
        has_horiz_long = any(is_horiz(e[1]) and abs(e[0] - b_len) < 1e-3 for e in es)
        if not has_horiz_long:
            continue
        if any(is_vert(e[1]) for e in es):
            continue
        return True
    return False


def spec_hex_tight(cells):
    for c in cells:
        if c['n'] != 6:
            continue
        v = c['v']
        cx = sum(p[0] for p in v) / 6
        cy = sum(p[1] for p in v) / 6
        vangs = sorted(math.degrees(math.atan2(p[1] - cy, p[0] - cx)) % 360.0 for p in v)
        has_left = any(abs(a - 180) < 0.5 for a in vangs)
        has_right = any(abs(a) < 0.5 or abs(a - 360) < 0.5 for a in vangs)
        if has_left and has_right:
            return True
    return False


def main():
    cases = [('arch_33336', 'hex'), ('laves_33336', 'pent')]
    for name, kind in cases:
        sp = g.load_app(name)
        print('=' * 72)
        print(name)
        sols = []
        for mirror in (False, True):
            for dg in [d * 0.25 for d in range(0, 1200)]:  # 0..300 step 0.25
                cells = transform_cells(sp['cells'], dg, mirror)
                ok = spec_hex_tight(cells) if kind == 'hex' else spec_laves_tight(cells)
                if ok:
                    sols.append((dg, mirror))
        sols = sorted(set((round(d, 3), m) for d, m in sols))
        print('  exact-spec solutions (deg, mirror): %s' % sols[:12])
        if sols:
            d0, m0 = sols[0]
            W = (sp['W'][0], 0.0)
            H = (sp['H'][0], sp['H'][1])
            Wm = (W[0], -W[1] if m0 else W[1])
            Hm = (H[0], -H[1] if m0 else H[1])
            Wr = ss.rotp(Wm, d0)
            Hr = ss.rotp(Hm, d0)
            area = abs(Wr[0] * Hr[1] - Wr[1] * Hr[0])
            print('   at deg=%.3f mirror=%s, rotated period W=(%.4f,%.4f) H=(%.4f,%.4f)' % (
                d0, m0, Wr[0], Wr[1], Hr[0], Hr[1]))
            print('   period area=%.4f  B(area/cell)=%.4f' % (area, area / 1.0))


if __name__ == '__main__':
    main()
