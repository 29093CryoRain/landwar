import json, math
with open('data/tiling_specs_laves.json') as f:
    spec = json.load(f)['laves_3636']
W = complex(spec['W'][0], spec['W'][1]); H = complex(spec['H'][0], spec['H'][1])
base=[]
for c in spec['cells']:
    base.append({'n':c['n'],'cx':complex(c['cx'],c['cy']),'v':[complex(p[0],p[1]) for p in c['v']]})
B=len(base)
def norm(v): return (round(v.real,5), round(v.imag,5))
def cell_poly(r,c,b):
    o=complex(c)*W+complex(r)*H
    return [v+o for v in base[b]['v']]
def cell_center(r,c,b):
    return complex(c)*W+complex(r)*H+base[b]['cx']
cells=[]
for r in range(-3,4):
    for c in range(-3,4):
        for b in range(B):
            cells.append((r,c,b))
def edges_of(r,c,b):
    poly=cell_poly(r,c,b); n=len(poly)
    return [(poly[i],poly[(i+1)%n]) for i in range(n)]
eo={}
for (r,c,b) in cells:
    for A,Bv in edges_of(r,c,b):
        eo[(norm(A),norm(Bv))]=(r,c,b)
def neighbor_across(r,c,b,i):
    poly=cell_poly(r,c,b); A=poly[i]; Bv=poly[(i+1)%len(poly)]
    return eo.get((norm(Bv),norm(A)))
def neighbors_of(r,c,b):
    return [neighbor_across(r,c,b,i) for i in range(len(base[b]['v'])) if neighbor_across(r,c,b,i)]
def offsets(anchor,members):
    acx=cell_center(*anchor)
    out=[]
    for (r,c,b) in members:
        cc=cell_center(r,c,b)
        out.append((round(cc.real-acx.real,12), round(cc.imag-acx.imag,12)))
    return sorted(set(out), key=lambda t:(t[0],t[1]))
anchor=(0,0,0)
print('anchor',anchor)
print('L1', offsets(anchor,[anchor]))
# vertex -> incident cells
from collections import defaultdict
vert_map=defaultdict(list)
for (r,c,b) in cells:
    for v in cell_poly(r,c,b):
        vert_map[norm(v)].append((r,c,b))
# find a 3-valent vertex near origin
for k,celllist in vert_map.items():
    if len(celllist)==3:
        z=complex(k[0],k[1])
        if abs(z.real)<3 and abs(z.imag)<3:
            print('3-valent vertex',z,'cells',celllist)
            print('L3', offsets(celllist[0], celllist))
            break
# find a 6-valent vertex near origin
for k,celllist in vert_map.items():
    if len(celllist)==6:
        z=complex(k[0],k[1])
        if abs(z.real)<3 and abs(z.imag)<3:
            print('6-valent vertex',z)
            print('L6', offsets(celllist[0], celllist))
            break
# L5: central rhombus + edge neighbors
nb=neighbors_of(*anchor)
print('anchor neighbors',nb)
print('L5', offsets(anchor,[anchor]+nb))
# L12: L6 + all edge-adjacent
# need 6-valent celllist
for k,celllist in vert_map.items():
    if len(celllist)==6:
        z=complex(k[0],k[1])
        if abs(z.real)<3 and abs(z.imag)<3:
            l6=set(celllist)
            l12=set(celllist)
            for cell in celllist:
                for n2 in neighbors_of(*cell):
                    if n2: l12.add(n2)
            print('L12', offsets(celllist[0], list(l12)))
            break
