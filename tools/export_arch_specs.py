#!/usr/bin/env python3
# Export working Archimedean tiling fundamental domains to data/tiling_specs_arch.json.
# Uses gen_arch2.py propagation. Domains are rotated to W=(wx,0) and then re-bucketed into a
# RECTANGULAR period W=(wx,0), H=(0,hy2) where hy2 is the smallest vertical period
# (hx/wx is rational; for the supported tilings it is 1/2 or 0). This keeps the C++ table
# driver axis-aligned (same wrap model as hex).
import importlib.util, math, json, os
from fractions import Fraction

spec = importlib.util.spec_from_file_location('g', 'tools/gen_arch2.py')
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

CONFIGS = [
    ([3,4,6,4], math.sqrt(6/(3+2*math.sqrt(3))), 'arch_3464'),
    ([3,6,3,6], math.sqrt(math.sqrt(3)/2), 'arch_3636'),
    ([3,12,12], 1/math.sqrt(2+7*math.sqrt(3)/6), 'arch_31212'),
    ([4,6,12], 1/math.sqrt(1.5+math.sqrt(3)), 'arch_4612'),
    ([4,8,8], 2-math.sqrt(2), 'arch_488'),
]

def rot(z, ang):
    c = math.cos(ang); s = math.sin(ang)
    return complex(z.real*c - z.imag*s, z.real*s + z.imag*c)

def build_rect_domain(cfg, L, seqs):
    polys, err = g.generate(cfg, L, seqs, max_polys=2500)
    if polys is None:
        print('generate fail', cfg, err)
        return None
    W, H = g.find_period(polys)
    if W is None or H is None:
        print('no period', cfg)
        return None
    if W.real*H.imag - W.imag*H.real < 0:
        W, H = H, W
    # rotate so W is horizontal
    ang = -math.atan2(W.imag, W.real)
    for p in polys:
        p.v = [rot(v, ang) for v in p.v]
    Wr = rot(W, ang)
    Hr = rot(H, ang)
    wx = abs(Wr)
    hx = Hr.real
    hy = Hr.imag
    if hy < 0:
        Hr = -Hr
        hy = Hr.imag
        hx = Hr.real
    # Smallest vertical period: m*H.x must be integer multiple of W.x.
    frac = Fraction(float(hx / wx)).limit_denominator(8)
    m = frac.denominator
    hy2 = hy * m
    # Collect base cells modulo the rectangular domain.
    seen = []
    cells = []
    for p in polys:
        cx = sum(p.v) / p.n
        u = cx.real / wx
        v = cx.imag / hy2
        u -= math.floor(u)
        v -= math.floor(v)
        if u > 0.98: u = 0.0
        if v > 0.98: v = 0.0
        dup = False
        for su, sv in seen:
            if abs(su-u) < 0.02 and abs(sv-v) < 0.02:
                dup = True
                break
        if dup:
            continue
        seen.append((u, v))
        bc = complex(u*wx, v*hy2)
        shift = bc - cx
        vv = [z + shift for z in p.v]
        cells.append({'n': p.n, 'cx': bc.real, 'cy': bc.imag,
                      'v': [[round(z.real, 12), round(z.imag, 12)] for z in vv]})
    print('OK', cfg, 'W', [round(wx,12),0.0], 'H', [0.0,round(hy2,12)], 'cells', len(cells))
    return {'cfg': cfg, 'L': L, 'W': [round(wx,12), 0.0], 'H': [0.0, round(hy2,12)], 'cells': cells}

def main():
    out = {}
    for cfg, L, name in CONFIGS:
        per = {n: g.valid_seqs(cfg, n) for n in set(cfg)}
        seqs = {n: per[n][0] for n in per}
        d = build_rect_domain(cfg, L, seqs)
        if d is None:
            print('FAILED', name)
            continue
        for c in d['cells']:
            c['cx'] = round(c['cx'], 12)
            c['cy'] = round(c['cy'], 12)
        out[name] = d
    path = os.path.join('data', 'tiling_specs_arch.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
    print('wrote', path)

if __name__ == '__main__':
    main()
