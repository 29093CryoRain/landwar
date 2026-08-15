import math, cmath
from collections import defaultdict
L = math.sqrt(6/(2+math.sqrt(3)))
r3 = L/math.sqrt(3); r4 = L/math.sqrt(2)
pts=[]
for a in [30,105,180,255,330]:
    rad=math.radians(a); r=r3 if a in (30,180,330) else r4
    pts.append(complex(r*math.cos(rad), r*math.sin(rad)))
# ensure CCW (compute signed area)
def signed_area(poly):
    s=0
    for i in range(len(poly)):
        a=poly[i]; b=poly[(i+1)%len(poly)]
        s+=a.real*b.imag-a.imag*b.real
    return s/2
print('signed area',signed_area(pts))
if signed_area(pts)<0: pts=list(reversed(pts))
seed=pts
SCALE=1e7
def norm(v): return (round(v.real*SCALE), round(v.imag*SCALE))
def reflect_poly(poly,A,B):
    dx=B.real-A.real; dy=B.imag-A.imag; L2=dx*dx+dy*dy
    out=[]
    for x,y in [(z.real,z.imag) for z in poly]:
        t=((x-A.real)*dx+(y-A.imag)*dy)/L2
        px=A.real+t*dx; py=A.imag+t*dy
        out.append(complex(2*px-x,2*py-y))
    return list(reversed(out))
def edges(poly):
    return [(poly[i],poly[(i+1)%5]) for i in range(5)]
polys=[seed]; eo={}
for i,(A,B) in enumerate(edges(seed)): eo[(norm(A),norm(B))]=0
q=[0]; processed=set()
while q and len(polys)<3000:
    pi=q.pop(0)
    if pi in processed: continue
    processed.add(pi)
    p=polys[pi]
    for A,B in edges(p):
        if (norm(B),norm(A)) in eo: continue
        np=reflect_poly(p,A,B)
        ni=len(polys); polys.append(np); q.append(ni)
        for C,D in edges(np):
            kk=(norm(C),norm(D))
            if kk in eo:
                print('overlap',pi,ni); raise SystemExit
            eo[kk]=ni
print('polys',len(polys))
verts=set()
for p in polys:
    for v in p: verts.add(norm(v))
pts_w=[complex(k[0]/SCALE,k[1]/SCALE) for k in verts]
central=[z for z in pts_w if abs(z.real)<5 and abs(z.imag)<5]
def is_trans(v,tol=0.05):
    for z in central:
        t=z+v
        if not any(abs(w.real-t.real)<tol and abs(w.imag-t.imag)<tol for w in pts_w):
            return False
    return True
cands=[]
for a in central:
    for b in central:
        d=b-a
        if abs(d)>1e-6: cands.append(d)
passed=[]; seen=set()
for v in cands:
    key=(round(v.real,2),round(v.imag,2))
    if key in seen: continue
    seen.add(key)
    if abs(v)<0.2: continue
    if abs(v.real)>20 or abs(v.imag)>20: continue
    if is_trans(v): passed.append(v)
print('passed',len(passed))
for v in passed[:20]: print(v)
