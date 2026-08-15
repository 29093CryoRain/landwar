import json, math, itertools
with open('data/tiling_specs_laves.json') as f:
    spec = json.load(f)['laves_488']
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
    return sorted(set((round(cell_center(r,c,b).real-acx.real,12), round(cell_center(r,c,b).imag-acx.imag,12)) for (r,c,b) in members), key=lambda t:(t[0],t[1]))
def boundary_edges(member_set):
    edges=set()
    for (r,c,b) in member_set:
        for A,Bv in edges_of(r,c,b):
            key=(norm(A),norm(Bv)); rkey=(norm(Bv),norm(A))
            if rkey in edges: edges.discard(rkey)
            else: edges.add(key)
    # convert back to complex endpoints
    return [(complex(A[0],A[1]), complex(B[0],B[1])) for A,B in edges]
def merge_collinear(edges):
    # edges set of (A,B) as complex. Greedy merge collinear sharing endpoint.
    elist=list(edges)
    changed=True
    while changed:
        changed=False
        for i in range(len(elist)):
            for j in range(i+1,len(elist)):
                A,Bv=elist[i]; C,D=elist[j]
                if norm(A)==norm(D) or norm(Bv)==norm(C):
                    # check collinear: cross of (B-A, C-A)
                    d1=Bv-A; d2=C-A if norm(A)==norm(D) else D-Bv
                    if abs(d1.real*d2.imag-d1.imag*d2.real)<1e-7:
                        # merge into longest segment
                        pts=[A,Bv,C,D]
                        xs=[p.real for p in pts]; ys=[p.imag for p in pts]
                        lo=min(range(4), key=lambda k:(xs[k],ys[k]))
                        hi=max(range(4), key=lambda k:(xs[k],ys[k]))
                        elist[i]=(pts[lo],pts[hi]); elist.pop(j)
                        changed=True; break
            if changed: break
    return elist
anchor=(0,0,0)
seen=set([anchor])
for _ in range(3):
    new=set(seen)
    for cell in list(seen):
        for nb in neighbors_of(*cell):
            if nb: new.add(nb)
    seen=new
print('seen',len(seen))
for size in (4,8):
    found=False
    for combo in itertools.combinations(seen,size):
        if anchor not in combo: continue
        s=set(combo)
        stack=[anchor]; vis=set([anchor])
        while stack:
            x=stack.pop()
            for nb in neighbors_of(*x):
                if nb and nb in s and nb not in vis:
                    vis.add(nb); stack.append(nb)
        if len(vis)!=size: continue
        be=boundary_edges(s)
        merged=merge_collinear(list(be))
        if len(merged)==(3 if size==4 else 4):
            print('L'+str(size), offsets(anchor,s), 'merged',len(merged))
            found=True
            break
    print('size',size,'found',found)
