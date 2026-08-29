import json, math
from collections import defaultdict
with open('data/tiling_specs_arch.json') as f:
    spec = json.load(f)['arch_3636']
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
def is_hex(cell): return base[cell[2]]['n']==6
def is_tri(cell): return base[cell[2]]['n']==3
# find hex anchor near origin
hex_anchor=None; tri_anchor=None
for (r,c,b) in cells:
    if r==0 and c==0 and base[b]['n']==6: hex_anchor=(r,c,b)
    if r==0 and c==0 and base[b]['n']==3: tri_anchor=(r,c,b)
print('anchors',hex_anchor,tri_anchor)
# L2: single hex
print('L2', offsets(hex_anchor,[hex_anchor]))
# L3: hex + top/bottom triangle neighbors (edge direction horizontal)
poly=cell_poly(*hex_anchor); n=len(poly)
topbot=[]
for i in range(n):
    A=poly[i]; Bv=poly[(i+1)%n]
    d=Bv-A
    if abs(d.real)<1e-7:  # vertical edge（本生成朝向：六边形顶点在正上下方）
        nb=neighbor_across(*hex_anchor,i)
        if nb and is_tri(nb): topbot.append(nb)
print('topbot',topbot)
print('L3', offsets(hex_anchor,[hex_anchor]+topbot))
# vertex map
vert_map=defaultdict(list)
for (r,c,b) in cells:
    for v in cell_poly(r,c,b):
        vert_map[norm(v)].append((r,c,b))
# L5: vertex with 2 tri + 2 hex, symmetric. Find vertex incident cells matching.
for k,celllist in vert_map.items():
    if len(celllist)!=4: continue
    tri_count=sum(1 for x in celllist if is_tri(x))
    hex_count=sum(1 for x in celllist if is_hex(x))
    z=complex(k[0],k[1])
    if tri_count==2 and hex_count==2 and abs(z.real)<3 and abs(z.imag)<3:
        # choose anchor = one hexagon in the set
        anchor=[x for x in celllist if is_hex(x)][0]
        print('L5', offsets(anchor,celllist))
        break
# L8: triangle + all cells sharing a vertex with it
tri_poly=cell_poly(*tri_anchor)
shared=set([tri_anchor])
for v in tri_poly:
    for cell in vert_map.get(norm(v),[]):
        shared.add(cell)
print('L8', offsets(tri_anchor,list(shared)), 'count', len(shared))
