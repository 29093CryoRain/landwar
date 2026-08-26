#!/usr/bin/env python3
"""build_fixed.py — apply transform (mirror + theta) to appendix, refold to period, validate.
Outputs a candidate spec for arch_33434 (axis-aligned) and reports spec-predicate + adjacency."""
import json, math, os, sys, copy
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


def build(name, deg, mirror, want_axis=True, ring=6, limit=80):
    sp = g.load_app(name)
    cells = transform_cells(sp['cells'], deg, mirror)
    # transform period too
    W, H = (sp['W'][0], sp['W'][1]), (sp['H'][0], sp['H'][1])
    # mirror about x-axis: W stays (wx, 0), H=(hx,hy) -> (hx, -hy); then rot
    W = (W[0], -W[1] if mirror else W[1])
    H = (H[0], -H[1] if mirror else H[1])
    W = ss.rotp(W, deg)
    H = ss.rotp(H, deg)
    # force W horizontal (wx,0) if we want axis-aligned: find horizontal combos
    if want_axis:
        res = g.axis_period(W, H, deg, limit)
        if res:
            area, hl, vl, ch, cv, cb = res
            nW = (abs(ch[0] * W[0] + ch[1] * H[0]), 0.0)
            nH = (0.0, abs(cv[0] * W[1] + cv[1] * H[1]))
        else:
            return None, W, H
    else:
        nW, nH = W, H
    base = g.fold_cells(cells, W, H, nW, nH, ring=ring)
    return {'W': [round(nW[0], 12), 0.0], 'H': [0.0, round(nH[1], 12)],
            'cells': base, 'B': len(base), 'edge': g.edge_angle_set(base),
            'adj': g.adjacency(base, nW[0], 0.0, nH[1]),
            'nW': nW, 'nH': nH}, nW, nH


def main():
    # arch_33434: mirror=y + rot90 (from spec search), axis-aligned
    for (deg, mir) in [(90, True), (0, True), (270, True)]:
        r, nW, nH = build('arch_33434', deg, mir, True)
        if r is None:
            print('no axis for arch_33434 mirror=%s deg=%d' % (mir, deg))
            continue
        print('arch_33434 mir=%s deg=%d -> B=%d W=%s H=%s edge=%s adj=%d/%d' % (
            mir, deg, r['B'], r['W'], r['H'], r['edge'], r['adj'][0], r['adj'][1]))
        print('   maybe_B=%d' % (int(round(r['W'][0] * r['H'][1]))))


if __name__ == '__main__':
    main()
