#!/usr/bin/env python3
"""emit_data.py — emit FINAL spec JSON for arch_33434 / arch_33336 / laves_33336 into
tools/blocks/final_<name>.json. Determines transforms:
  arch_33434 : transpose (mirror=y + rot90), axis-aligned B=6 (verified spec+adjacency).
  arch_33336 : mirror(y) + rot to 左右六边形, parallelogram B=18.
  laves_33336: rot to 长边水平, parallelogram B=12.
Validates W.y-aware adjacency + area + key direction-spec feature, prints a JSON summary."""
import json, math, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g
import tools.gen_parall as gp
import tools.spec_search as ss
import tools.build_fixed as bf


def adj_full(cells, W, H, nbr=3):
    wx, wy, hx, hy = W[0], W[1], H[0], H[1]
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
                    oy = dc * wy + dr * hy
                    for nb, oc in enumerate(cells):
                        if found:
                            break
                        ov = oc['v']
                        for kk in range(oc['n']):
                            n0 = (ov[kk][0] + ox, ov[kk][1] + oy)
                            n1 = (ov[(kk + 1) % oc['n']][0] + ox, ov[(kk + 1) % oc['n']][1] + oy)
                            if math.hypot(n0[0] - Bv[0], n0[1] - Bv[1]) < 1e-4 and \
                               math.hypot(n1[0] - A[0], n1[1] - A[1]) < 1e-4:
                                found = True
                                break
            tot += 1
            if found:
                ok += 1
            else:
                miss.append((b, k))
    return ok, tot, miss


def area(c):
    v = c['v']
    s = 0.0
    for i in range(len(v)):
        a, b = v[i], v[(i + 1) % len(v)]
        s += a[0] * b[1] - b[0] * a[1]
    return abs(s) * 0.5


def main():
    out = {}

    # arch_33434: transpose (deg=90, mir=True) folded axis-aligned
    r, nW, nH = bf.build('arch_33434', 90, True, True)
    cells = r['cells']
    spec = {'W': [round(nW[0], 12), 0.0], 'H': [0.0, round(nH[1], 12)],
            'cells': [{'n': c['n'], 'cx': round(c['cx'], 12), 'cy': round(c['cy'], 12),
                       'v': [[round(p[0], 12), round(p[1], 12)] for p in c['v']]} for c in cells]}
    ok, tot, miss = adj_full(cells, (nW[0], 0.0), (0.0, nH[1]))
    spec_ok = ss.spec_arch33434(cells, nW[0], 0.0, nH[1])
    areas = [area(c) for c in cells]
    print('arch_33434: B=%d adj=%d/%d spec=%s area(min/max)=%.4f/%.4f' % (len(cells), ok, tot, spec_ok, min(areas), max(areas)))
    out['arch_33434'] = spec

    # arch_33336: mirror + rot (+40.893) -> 左右六边形, parallelogram
    sp = g.load_app('arch_33336')
    th, mir = 40.893, True
    cells, W, H = gp.rotate_spec_and_period(sp, th, mir)
    ok, tot, miss = adj_full(cells, W, H)
    print('arch_33336: mirror+rotd=%.3f  W=(%.5f,%.5f) H=(%.5f,%.5f) area=%.4f B=%d adj=%d/%d' %
          (th, W[0], W[1], H[0], H[1], abs(W[0]*H[1]-W[1]*H[0]), len(cells), ok, tot))
    out['arch_33336'] = {'W': [round(W[0], 12), round(W[1], 12)], 'H': [round(H[0], 12), round(H[1], 12)],
                         'cells': [{'n': c['n'], 'cx': round(c['cx'], 12), 'cy': round(c['cy'], 12),
                                    'v': [[round(p[0], 12), round(p[1], 12)] for p in c['v']]} for c in cells]}

    # laves_33336: rot (+49.107) -> long edge horizontal, parallelogram
    sp = g.load_app('laves_33336')
    th, mir = 49.107, False
    cells, W, H = gp.rotate_spec_and_period(sp, th, mir)
    ok, tot, miss = adj_full(cells, W, H)
    print('laves_33336: rot=%.3f  W=(%.5f,%.5f) H=(%.5f,%.5f) area=%.4f B=%d adj=%d/%d' %
          (th, W[0], W[1], H[0], H[1], abs(W[0]*H[1]-W[1]*H[0]), len(cells), ok, tot))
    out['laves_33336'] = {'W': [round(W[0], 12), round(W[1], 12)], 'H': [round(H[0], 12), round(H[1], 12)],
                          'cells': [{'n': c['n'], 'cx': round(c['cx'], 12), 'cy': round(c['cy'], 12),
                                     'v': [[round(p[0], 12), round(p[1], 12)] for p in c['v']]} for c in cells]}

    # verify laves long edge horizontal + no vertical
    def pent_horiz(cells):
        for c in cells:
            if c['n'] != 5:
                continue
            es = ss.pent_edges(c)
            lens = sorted(e[0] for e in es)
            bl = lens[-1]
            has = any(abs(e[1]) < 0.5 or abs(e[1]-180) < 0.5 for e in es if abs(e[0]-bl) < 1e-3)
            nv = all(not (abs(e[1]-90) < 0.5) for e in es)
            if has and nv:
                return True
        return False
    print('   laves_33336 long-edge-horizontal/no-vertical:', pent_horiz(out['laves_33336']['cells']))

    os.makedirs(os.path.join(ROOT, 'tools/blocks'), exist_ok=True)
    for name, spec in out.items():
        json.dump(spec, open(os.path.join(ROOT, 'tools/blocks', 'final_%s.json' % name), 'w'), indent=1)
        print('wrote final_%s.json' % name)


if __name__ == '__main__':
    main()
