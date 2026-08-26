#!/usr/bin/env python3
"""Recon for snub tilings. Loads appendix (raw) + current data, checks tiling validity
(per-edge adjacency), and reports cell edge-angle distributions + short/long edge details
so we can determine the direction-spec rotation theta."""
import json, math, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'rubbish', '扭棱密铺构造解决方案_附录.json')
CUR_A = os.path.join(ROOT, 'data', 'tiling_specs_arch.json')
CUR_L = os.path.join(ROOT, 'data', 'tiling_specs_laves.json')

TOL = 1e-5


def load(fp, name=None):
    d = json.load(open(fp, encoding='utf-8'))
    if name:
        return d[name]
    return d


def check_adjacency(cells, wx, hx, hy, nbr=2):
    """Each directed edge of each cell must find a reverse-matching edge within dr,dc in [-nbr,nbr]."""
    B = len(cells)
    ok_edges = 0
    total_edges = 0
    missing = []
    for b, c in enumerate(cells):
        v = c['v']
        for k in range(c['n']):
            A = v[k]
            Bv = v[(k + 1) % c['n']]
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
                            if dist(n0, Bv) < TOL and dist(n1, A) < TOL:
                                found = True
                                break
            total_edges += 1
            if found:
                ok_edges += 1
            else:
                missing.append((b, k))
    return ok_edges, total_edges, missing


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def edge_stats(c, rel=None):
    """Returns list of (length, angle_degrees_undirected_in_0_180)."""
    v = c['v']
    cx = c['cx'] if not rel else rel[0]
    cy = c['cy'] if not rel else rel[1]
    out = []
    for k in range(c['n']):
        A = v[k]
        Bv = v[(k + 1) % c['n']]
        dx, dy = Bv[0] - A[0], Bv[1] - A[1]
        ln = math.hypot(dx, dy)
        ang = math.degrees(math.atan2(dy, dx))
        # normalize to [0,180)
        ang = ang % 180.0
        out.append((ln, ang))
    return out


def main():
    app = load(APP)
    cur = {}
    cur.update(load(CUR_A))
    cur.update(load(CUR_L))
    for name in ['arch_33434', 'arch_33336', 'laves_33336']:
        a = app[name]
        wx, hx, hy = a['W'][0], a['H'][0], a['H'][1]
        ok, tot, miss = check_adjacency(a['cells'], wx, hx, hy)
        missing = miss[:8]
        print('==== ', name)
        print('  appendix W=(%.4f,%.4f) H=(%.4f,%.4f)  B=%d  adj %d/%d  missing=%s'
              % (wx, a['W'][1], hx, hy, len(a['cells']), ok, tot, missing))
        # edge lengths histogram
        lens = {}
        for c in a['cells']:
            for ln, ang in edge_stats(c):
                key = round(ln, 4)
                found = None
                for kk in lens:
                    if abs(kk - key) < 0.02:
                        found = kk
                        break
                lens.setdefault(found if found is not None else key, []).append(ang)
        for ln0, ar in sorted(lens.items()):
            angs = sorted(set(round(a1, 2) for a1 in ar))
            print('  appendix len~%.4f (%d edges) angles=%s' % (ln0, len(ar), angs))
        if name in cur:
            ca = cur[name]
            cwx, chx, chy = ca['W'][0], ca['H'][0], ca['H'][1]
            ok2, tot2, miss2 = check_adjacency(ca['cells'], cwx, chx, chy)
            print('  current  W=(%.4f,%.4f) H=(%.4f,%.4f)  B=%d  adj %d/%d  missing=%s'
                  % (cwx, ca['W'][1], chx, chy, len(ca['cells']), ok2, tot2, miss2[:8]))
            lens2 = {}
            for c in ca['cells']:
                for ln, ang in edge_stats(c):
                    key = round(ln, 4)
                    found = None
                    for kk in lens2:
                        if abs(kk - key) < 0.02:
                            found = kk
                            break
                    lens2.setdefault(found if found is not None else key, []).append(ang)
            for ln0, ar in sorted(lens2.items()):
                angs = sorted(set(round(a1, 2) for a1 in ar))
                print('  current  len~%.4f (%d edges) angles=%s' % (ln0, len(ar), angs))
        print()


if __name__ == '__main__':
    main()
