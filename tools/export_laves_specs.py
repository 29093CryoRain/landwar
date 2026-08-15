#!/usr/bin/env python3
# Export Laves (dual) fundamental domains for the 5 Arch tilings whose Arch specs exist.
# Dual cells = vertex cells of the Arch tiling; scaled to area 1. Domain is the same
# rectangular period as the Arch tiling.
import importlib.util, math, json, os
from collections import defaultdict, Counter
from fractions import Fraction

spec = importlib.util.spec_from_file_location('g', 'tools/gen_arch2.py')
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

CONFIGS = [
    ([3,4,6,4], math.sqrt(6/(3+2*math.sqrt(3))), 'laves_3464'),
    ([3,6,3,6], math.sqrt(math.sqrt(3)/2), 'laves_3636'),
    ([3,12,12], 1/math.sqrt(2+7*math.sqrt(3)/6), 'laves_31212'),
    ([4,6,12], 1/math.sqrt(1.5+math.sqrt(3)), 'laves_4612'),
    ([4,8,8], 2-math.sqrt(2), 'laves_488'),
]

def rot(z, ang):
    c = math.cos(ang); s = math.sin(ang)
    return complex(z.real*c - z.imag*s, z.real*s + z.imag*c)

def build_laves_domain(cfg, L, seqs):
    polys, err = g.generate(cfg, L, seqs, max_polys=2500)
    if polys is None:
        print('generate fail', cfg, err); return None
    W, H = g.find_period(polys)
    if W is None:
        print('no period', cfg); return None
    if W.real*H.imag - W.imag*H.real < 0:
        W, H = H, W
    ang = -math.atan2(W.imag, W.real)
    for p in polys:
        p.v = [rot(v, ang) for v in p.v]
    Wr = rot(W, ang); Hr = rot(H, ang)
    wx = abs(Wr); hx = Hr.real; hy = Hr.imag
    if hy < 0:
        Hr = -Hr; hy = Hr.imag; hx = Hr.real
    frac = Fraction(float(hx / wx)).limit_denominator(8)
    m = frac.denominator
    hy2 = hy * m
    # vertex -> incident polygon ids
    centers = [sum(p.v) / p.n for p in polys]
    vert_map = defaultdict(list)
    for pi, p in enumerate(polys):
        for v in p.v:
            vert_map[(round(v.real, 5), round(v.imag, 5))].append(pi)
    duals = []
    for key, pis in vert_map.items():
        if len(pis) != len(cfg):
            continue
        v = complex(key[0], key[1])
        pts = [centers[pi] for pi in pis]
        pts.sort(key=lambda z: math.atan2(z.imag - v.imag, z.real - v.real))
        # deduplicate adjacent identical (rounding) points
        clean = []
        for z in pts:
            if not clean or abs(z - clean[-1]) > 1e-7:
                clean.append(z)
        if len(clean) >= 3:
            duals.append(clean)
    if not duals:
        print('no duals', cfg); return None
    # cell area (all congruent)
    def poly_area(pts):
        s = 0.0
        for i in range(len(pts)):
            a = pts[i]; b = pts[(i+1) % len(pts)]
            s += a.real*b.imag - a.imag*b.real
        return abs(s) / 2.0
    area0 = poly_area(duals[0])
    scale = 1.0 / math.sqrt(area0)
    print('cfg', cfg, 'dual cells', len(duals), 'raw area', area0, 'scale', scale)
    # scale cells AND period vectors so each Laves cell area = 1.
    wx *= scale
    hy2 *= scale
    # collect base cells modulo scaled rectangular domain
    seen = []
    cells = []
    for pts in duals:
        spts = [z * scale for z in pts]
        cx = sum(spts) / len(spts)
        u = cx.real / wx
        v = cx.imag / hy2
        u -= math.floor(u); v -= math.floor(v)
        if u > 0.98: u = 0.0
        if v > 0.98: v = 0.0
        dup = False
        for su, sv in seen:
            if abs(su-u) < 0.03 and abs(sv-v) < 0.03:
                dup = True; break
        if dup:
            continue
        seen.append((u, v))
        bc = complex(u*wx, v*hy2)
        shift = bc - cx
        vv = [[round(z.real + shift.real, 12), round(z.imag + shift.imag, 12)] for z in spts]
        cells.append({'n': len(vv), 'cx': round(bc.real,12), 'cy': round(bc.imag,12), 'v': vv})
    print('base cells', len(cells), Counter(c['n'] for c in cells))
    return {'W': [round(wx,12), 0.0], 'H': [0.0, round(hy2,12)], 'cells': cells}

def main():
    out = {}
    for cfg, L, name in CONFIGS:
        per = {n: g.valid_seqs(cfg, n) for n in set(cfg)}
        seqs = {n: per[n][0] for n in per}
        d = build_laves_domain(cfg, L, seqs)
        if d is None:
            print('FAILED', name)
            continue
        out[name] = d
    path = os.path.join('data', 'tiling_specs_laves.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
    print('wrote', path)

if __name__ == '__main__':
    main()
