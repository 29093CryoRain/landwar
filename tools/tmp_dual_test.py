import math, importlib.util, json
from collections import defaultdict
spec=importlib.util.spec_from_file_location('g','tools/gen_arch2.py')
g=importlib.util.module_from_spec(spec); spec.loader.exec_module(g)

def dual_test(cfg,L,seqs,name,max_polys=1200):
    polys,err=g.generate(cfg,L,seqs,max_polys=max_polys,keep_on_mismatch=True)
    if polys is None:
        print('gen fail',err); return
    W,H=g.find_period(polys)
    if W is None:
        print('no period'); return
    # rotate W horizontal (same as build_rect_domain)
    if W.real*H.imag-W.imag*H.real<0: W,H=H,W
    ang=-math.atan2(W.imag,W.real)
    c=math.cos(ang); s=math.sin(ang)
    for p in polys:
        p.v=[complex(v.real*c-v.imag*s, v.real*s+v.imag*c) for v in p.v]
    Wr=complex(abs(W),0); Hr=complex(H.real*c-H.imag*s, H.real*s+H.imag*c)
    wx=abs(Wr); hx=Hr.real; hy=Hr.imag
    # use vertical period for dual too? For prototype use slanted W,H? We'll use original rotated W,H (slanted maybe)
    # Build vertex map: rounded vertex -> list of polygon ids and centers
    vert_map=defaultdict(list)
    centers=[]
    for pi,p in enumerate(polys):
        cx=sum(p.v)/p.n
        centers.append(cx)
        for v in p.v:
            key=(round(v.real,5),round(v.imag,5))
            vert_map[key].append(pi)
    # For each vertex, if incidence == len(cfg), build dual cell (sorted centers by angle)
    duals=[]
    for key,pis in vert_map.items():
        if len(pis) != len(cfg):  # boundary or duplicate keys
            # Try deduplicate near vertices? skip for now
            continue
        v=complex(key[0],key[1])
        pts=[centers[pi] for pi in pis]
        pts.sort(key=lambda z: math.atan2(z.imag-v.imag, z.real-v.real))
        duals.append(pts)
    print(name,'vertices',len(vert_map),'dual cells',len(duals))
    if duals:
        # print first dual cell and area
        pts=duals[0]
        area=0
        for i in range(len(pts)):
            a=pts[i]; b=pts[(i+1)%len(pts)]
            area += a.real*b.imag - a.imag*b.real
        print(' first dual n',len(pts),'area',abs(area)/2)
        # show cells with distinct side counts
        from collections import Counter
        print(Counter(len(d) for d in duals))

dual_test([3,6,3,6], math.sqrt(math.sqrt(3)/2), {3:[6,6,6],6:[3,3,3,3,3,3]}, 'Arch3636')
dual_test([4,8,8], 2-math.sqrt(2), {4:[8,8,8,8],8:[4,8,4,8,4,8,4,8]}, 'Arch488')
