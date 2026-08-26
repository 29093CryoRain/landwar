#!/usr/bin/env python3
"""final_gen.py — produce the FINAL corrected spec data for the 3 crooked snub tilings.

arch_33434  : axis-aligned, B=6, transpose (chirality flip) - verified spec + adjacency.
arch_33336  : parallelogram, mirror(right-handed)+rotate to 左右六边形.
laves_33336 : parallelogram, rotate to 长边(水平)+no vertical.

Determines the exact spec-angle for each and writes candidate JSON files for review."""
import json, math, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g
import tools.gen_parall as gp
import tools.spec_search as ss
import tools.build_fixed as bf


def hex_base_edge_angle(cells):
    """hexagon edge base angle (mod 60) of a 左右-compatible hexagon; returns the apex base angle."""
    for c in cells:
        if c['n'] != 6:
            continue
        v = c['v']
        # vertex angles rel to centroid
        cx = sum(p[0] for p in v) / 6
        cy = sum(p[1] for p in v) / 6
        va = math.degrees(math.atan2(v[0][1] - cy, v[0][0] - cx)) % 60.0
        return va
    return None


def main():
    # ---- arch_33336: determine mirror + theta so hexagon is 左右 (apex at 0 mod 60) ----
    print('=== arch_33336 ===')
    sp = g.load_app('arch_33336')
    for mir in (False, True):
        # base apex angle of appendix hexagon in the computed (mirror)-applied orientation
        app_m = ss.transform(sp['cells'], 0.0, mir)
        base = hex_base_edge_angle(app_m)
        print('  mir=%s appendix hex apex base angle (mod 60) = %.4f' % (mir, base))
        # want apex at 0: theta = -base (mod 60). find theta in [0,60)
        th = (-base) % 60.0
        cells, W, H = gp.rotate_spec_and_period(sp, th, mir)
        # verify hexagon 左右
        okleft = any(abs((math.degrees(math.atan2(p[1] - (sum(q[1] for q in c['v'])/6),
                                                 p[0] - (sum(q[0] for q in c['v'])/6))) % 360.0) - 180) < 0.5
                     for c in cells[:1] for p in c['v']) if cells and cells[0]['n'] == 6 else False
        print('  chosen thetarot=%.4f mir=%s hex-left-vertex=%s' % (th, mir, okleft))
        # adjacency
        ok, tot, miss = gp.adj_full(cells, W, H) if hasattr(gp, 'adj_full') else (None, None, None)
        print('  W=(%.5f,%.5f) H=(%.5f,%.5f) area=%.4f B=%d' % (W[0], W[1], H[0], H[1],
              abs(W[0]*H[1]-W[1]*H[0]), len(cells)))
    print()
    print('=== laves_33336 ===')
    sp = g.load_app('laves_33336')
    for mir in (False, True):
        # long edge base angle (mod 60) after mirror
        class pent: pass
        cells_m = ss.transform(sp['cells'], 0.0, mir)
        base = None
        for c in cells_m:
            if c['n'] != 5:
                continue
            es = ss.pent_edges(c)
            lens = sorted(e[0] for e in es)
            bl = lens[-1]
            for ln, a in es:
                if abs(ln - bl) < 1e-4:
                    base = a % 60.0
                    break
            if base is not None:
                break
        th = (-base) % 60.0
        cells, W, H = gp.rotate_spec_and_period(sp, th, mir)
        print('  mir=%s long-edge base (mod 60)=%.4f thetarot=%.4f W=(%.5f,%.5f) H=(%.5f,%.5f) area=%.4f B=%d'
              % (mir, base, th, W[0], W[1], H[0], H[1], abs(W[0]*H[1]-W[1]*H[0]), len(cells)))


if __name__ == '__main__':
    main()
