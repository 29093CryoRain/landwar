#!/usr/bin/env python3
import json, math

with open('data/tiling_specs_arch.json') as f:
    spec = json.load(f)['arch_488']
W = complex(spec['W'][0], spec['W'][1])
H = complex(spec['H'][0], spec['H'][1])
base = []
for c in spec['cells']:
    base.append({'n': c['n'], 'cx': complex(c['cx'], c['cy']),
                 'v': [complex(p[0], p[1]) for p in c['v']]})
B = len(base)

# Build a 3x3 patch of cells: index (r,c,b)
def cell_poly(r, c, b):
    o = complex(c) * W + complex(r) * H
    return [v + o for v in base[b]['v']]

def cell_center(r, c, b):
    return complex(c) * W + complex(r) * H + base[b]['cx']

cells = []
for r in range(-2, 3):
    for c in range(-2, 3):
        for b in range(B):
            cells.append((r, c, b))

def norm(v): return (round(v.real, 6), round(v.imag, 6))

def edges_of(r, c, b):
    poly = cell_poly(r, c, b)
    n = len(poly)
    return [(poly[i], poly[(i+1) % n]) for i in range(n)]

# Build reverse edge lookup: for each directed edge, owner cell
eo = {}
for (r, c, b) in cells:
    for (A, Bv) in edges_of(r, c, b):
        eo[(norm(A), norm(Bv))] = (r, c, b)

def neighbor_across(r, c, b, i):
    poly = cell_poly(r, c, b)
    A = poly[i]; Bv = poly[(i+1) % len(poly)]
    return eo.get((norm(Bv), norm(A)))

def neighbors_of(r, c, b):
    out = []
    for i in range(len(base[b]['v'])):
        nb = neighbor_across(r, c, b, i)
        if nb is not None:
            out.append(nb)
    return out

# Pick anchors
def find_cell(n, r=0, c=0):
    # prefer origin cell
    for b in range(B):
        if base[b]['n'] == n:
            return (0, 0, b)
    for rr in range(-2,3):
        for cc in range(-2,3):
            for b in range(B):
                if base[b]['n'] == n:
                    return (rr,cc,b)
    return None

anchor_oct = find_cell(8)
anchor_sq = find_cell(4)
print('anchors', anchor_oct, anchor_sq)

def shape_offsets(anchor, members):
    acx = cell_center(*anchor)
    out = []
    for (r,c,b) in members:
        cc = cell_center(r,c,b)
        out.append((round(cc.real - acx.real, 12), round(cc.imag - acx.imag, 12)))
    return sorted(set(out), key=lambda t: (t[0], t[1]))

# L2: single octagon
shapes = {}
shapes['2'] = shape_offsets(anchor_oct, [anchor_oct])
# L3: octagon + its square neighbors
nb = neighbors_of(*anchor_oct)
sqs = [x for x in nb if base[x[2]]['n'] == 4]
shapes['3'] = shape_offsets(anchor_oct, [anchor_oct] + sqs)
# L7: square + its 4 octagon neighbors
nb = neighbors_of(*anchor_sq)
octs = [x for x in nb if base[x[2]]['n'] == 8]
shapes['7'] = shape_offsets(anchor_sq, [anchor_sq] + octs)
# L8: square + 4 octagons + the 4 squares between adjacent octagons around the square.
# The between squares are those adjacent to two of the surrounding octagons.
between = []
for s in cells:
    if base[s[2]]['n'] != 4 or s == anchor_sq: continue
    # count how many of the surrounding octagons are neighbors of this square
    cnt = 0
    for o in octs:
        if o in neighbors_of(*s): cnt += 1
    if cnt >= 2:
        between.append(s)
members = [anchor_sq] + octs + between
shapes['8'] = shape_offsets(anchor_sq, members)
# L4: edge between two octagons + the two squares at its endpoints.
oct_edges = []
poly = cell_poly(*anchor_oct)
for i in range(len(poly)):
    nb = neighbor_across(*anchor_oct, i)
    if nb and base[nb[2]]['n'] == 8:
        oct_edges.append((i, nb))
print('oct-oct edges', len(oct_edges))
if oct_edges:
    i, other = oct_edges[0]
    A = poly[i]
    Bv = poly[(i+1) % len(poly)]
    # square incident to A: among square neighbors of anchor_oct and other, find one sharing A or Bv.
    def shares_vertex(cell, vertex):
        cp = cell_poly(*cell)
        return any(abs(v - vertex) < 1e-7 for v in cp)
    sqs = set()
    for nb in neighbors_of(*anchor_oct):
        if base[nb[2]]['n'] == 4 and (shares_vertex(nb, A) or shares_vertex(nb, Bv)):
            sqs.add(nb)
    for nb in neighbors_of(*other):
        if base[nb[2]]['n'] == 4 and (shares_vertex(nb, A) or shares_vertex(nb, Bv)):
            sqs.add(nb)
    shapes['4'] = shape_offsets(anchor_oct, [anchor_oct, other] + list(sqs))

for k, v in shapes.items():
    print('level', k, 'cells', len(v), v)
