#!/usr/bin/env python3
"""Arch_33434 structure check: for each square (n=4), list edge-neighbor triangles' orientations.
Compare against spec block: squared + bottom-left right-triangle + bottom-right up-triangle."""
import json, math, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g


def tri_orient_deg(c):
    """Return the 'apex direction' angle of a triangle cell (degrees)."""
    # apex = vertex of the largest angle; use the vertex opposite the longest edge.
    v = c['v']
    n = len(v)
    edges = []
    for k in range(n):
        a, b = v[k], v[(k + 1) % n]
        edges.append((math.hypot(b[0] - a[0], b[1] - a[1]), k))
    e = sorted(edges, key=lambda x: -x[0])
    lo = e[0][1]      # longest edge (k->k+1); apex is the other vertex
    apex = (lo + 2) % n
    ax, ay = v[apex]
    return math.degrees(math.atan2(ay, ax)) % 360.0


def classify(deg):
    """triangle classification by apex angle."""
    d = deg % 360
    for name, ang in [('up', 90), ('down', 270), ('left', 180), ('right', 0)]:
        if abs(d - ang) < 1e-2 or abs(d - ang - 360) < 1e-2:
            return name
    return 'apex@%.1fdeg' % d


def main():
    sp = g.load_app('arch_33434')
    cells = sp['cells']
    # adjacency to find edge-neighbors
    wx, hx, hy = sp['W'][0], sp['H'][0], sp['H'][1]
    # build neighbor map: for edge k of cell b -> (nb, kk, dr, dc)
    def neigh_map(cells, wx, hx, hy, nbr=2):
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
                                if math.hypot(n0[0] - Bv[0], n0[1] - Bv[1]) < 1e-4 and \
                                   math.hypot(n1[0] - A[0], n1[1] - A[1]) < 1e-4:
                                    got = (nb, dr, dc, kk)
                                    break
                adj[(b, k)] = got
        return adj

    adj = neigh_map(cells, wx, hx, hy)
    # for print, compute direct neighbor list including dr,dc
    def neigh(b, k):
        return adj[(b, k)]
    sqs = [i for i, c in enumerate(cells) if c['n'] == 4]
    print('squares:', sqs)
    for b in sqs:
        c = cells[b]
        cx, cy = c['cx'], c['cy']
        # square edge orientations
        v = c['v']
        ed_ang = []
        for k in range(4):
            a, bb = v[k], v[(k + 1) % 4]
            ed_ang.append(math.degrees(math.atan2(bb[1] - a[1], bb[0] - a[0])) % 360.0)
        print('  square b=%d center(%.3f,%.3f) edge_dirs=%s' % (b, cx, cy, ['%.0f' % a for a in ed_ang]))
        for k in range(4):
            nb = neigh(b, k)
            if nb and nb[0] is not None:
                who, dr, dc, kk = nb
                oc = cells[who]
                a, bb = v[k], v[(k + 1) % 4]
                mid = ((a[0] + bb[0]) / 2, (a[1] + bb[1]) / 2)
                # direction from square center to shared edge midpoint
                dirdeg = math.degrees(math.atan2(mid[1] - cy, mid[0] - cx)) % 360.0
                if oc['n'] == 3:
                    to = classify(tri_orient_deg(oc))
                    print('      edge k=%d dir=%.0fdeg -> TRI n=%d orient=%s (b=%d)' % (k, dirdeg, oc['n'], to, who))
                else:
                    print('      edge k=%d dir=%.0fdeg -> n=%d (b=%d)' % (k, dirdeg, oc['n'], who))


if __name__ == '__main__':
    main()
