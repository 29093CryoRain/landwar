#!/usr/bin/env python3
# Generate Archimedean tiling fundamental domains by edge propagation.
# For configs where each polygon type has a unique cyclic edge-neighbor sequence,
# this produces a valid planar patch and detects a parallelogram period.
import math, json, sys
from collections import defaultdict, Counter

EPS = 1e-7
SCALE = 1e7

def norm(v):
    return (round(v.real*SCALE), round(v.imag*SCALE))

def rot_left(z, ang):
    c = math.cos(ang); s = math.sin(ang)
    return complex(z.real*c - z.imag*s, z.real*s + z.imag*c)

def point_in_poly(pt, poly):
    # only for centroid fill test (not used)
    pass

# ---- sequence generation ----
def neighbor_types(cfg, n):
    m = len(cfg)
    outs = set()
    for idx, t in enumerate(cfg):
        if t == n:
            pred = cfg[(idx-1)%m]
            succ = cfg[(idx+1)%m]
            outs.add(pred); outs.add(succ)
    return sorted(outs)

def pair_ok_unordered(cfg, n, a, b):
    m = len(cfg)
    for idx, t in enumerate(cfg):
        if t == n:
            pred = cfg[(idx-1)%m]
            succ = cfg[(idx+1)%m]
            if {a,b} == {pred,succ}:
                return True
    return False

def valid_seqs(cfg, n):
    alpha = neighbor_types(cfg, n)
    if not alpha:
        return []
    seqs = []
    def rec(seq):
        if len(seq) == n:
            if pair_ok_unordered(cfg, n, seq[-1], seq[0]):
                # canonical rotation: rotate so lexicographically smallest first
                s = tuple(seq)
                best = s
                for k in range(1,n):
                    t = s[k:]+s[:k]
                    if t < best: best = t
                if best not in [x[0] for x in seqs]:
                    seqs.append([best, list(seq)])
                return
            return
        for x in alpha:
            if len(seq) >= 1 and not pair_ok_unordered(cfg, n, seq[-1], x):
                continue
            rec(seq+[x])
    rec([])
    return [s for _,s in seqs]

# ---- propagation ----
class Poly:
    __slots__=('n','seq','v')
    def __init__(self,n,seq,v):
        self.n=n; self.seq=seq; self.v=v

def generate(cfg, L, seqs, max_polys=6000, debug=False, seedAngle=0.0, keep_on_mismatch=False):
    n0 = cfg[0]
    R = L / (2*math.sin(math.pi/n0))
    v = [R*complex(math.cos(2*math.pi*k/n0 + seedAngle), math.sin(2*math.pi*k/n0 + seedAngle)) for k in range(n0)]
    polys=[Poly(n0, list(seqs[n0]), v)]
    edge_owner={}
    for p in polys:
        for i in range(p.n):
            edge_owner[norm(p.v[i]), norm(p.v[(i+1)%p.n])] = 0
    q=[0]
    processed=set()
    fail=False
    while q and len(polys)<max_polys:
        pi=q.pop(0)
        if pi in processed: continue
        processed.add(pi)
        p=polys[pi]
        for i in range(p.n):
            a=p.v[i]; b=p.v[(i+1)%p.n]
            key=(norm(a), norm(b))
            rkey=(norm(b), norm(a))
            if rkey in edge_owner:
                other=edge_owner[rkey]
                op=polys[other]
                # find j in op such that op.v[j]=b, op.v[(j+1)%op.n]=a
                j=None
                for jj in range(op.n):
                    if norm(op.v[jj])==norm(b) and norm(op.v[(jj+1)%op.n])==norm(a):
                        j=jj; break
                if j is None:
                    if debug: print('conflict: reverse edge exists but no matching vertex edge', pi, i)
                    fail=True; break
                if not (p.seq[i]==op.n and op.seq[j]==p.n):
                    # Label mismatch between two polygons of correct geometric types
                    # (e.g., triangle edge label rotation). Repair triangle labels from
                    # actual neighbors immediately so future polygons are of correct types.
                    if debug: print('label mismatch', pi, i, 'p.n',p.n,'p.seq',p.seq[i], 'op.n',op.n,'op.seq',op.seq[j])
                    def fix_seq(poly):
                        if poly.n != 3: return
                        ns=[]
                        for e in range(3):
                            rk=(norm(poly.v[(e+1)%3]), norm(poly.v[e]))
                            if rk in edge_owner:
                                ns.append(polys[edge_owner[rk]].n)
                            else:
                                ns.append(poly.seq[e])
                        if ns.count(3)==1 and ns.count(4)==2:
                            poly.seq=ns
                    fix_seq(p)
                    fix_seq(op)
                continue
            m=p.seq[i]
            if m not in seqs:
                fail=True; break
            # Build m-gon with shared edge b->a in CCW order.
            nv=[b,a]
            for k in range(2,m):
                d=nv[-1]-nv[-2]
                nd=rot_left(d, 2*math.pi/m)
                nv.append(nv[-1]+nd)
            # rotate valid sequence so shared edge 0 matches current polygon type n
            base=seqs[m]
            rot=None
            for k in range(len(base)):
                if base[k]==p.n:
                    rot=base[k:]+base[:k]
                    break
            if rot is None:
                if debug: print('no rotation for seq', m, base, 'needed', n)
                fail=True; break
            # add polygon; check that the shared edge isn't already owned reversed? it's not
            # but some of its other edges may coincide with existing edges -> overlap detection later
            pi_new=len(polys)
            polys.append(Poly(m, rot, nv))
            q.append(pi_new)
            for jj in range(m):
                kk=(norm(nv[jj]), norm(nv[(jj+1)%m]))
                if kk in edge_owner:
                    if debug: print('conflict: directed edge already owned while adding poly', pi, i, 'jj', jj)
                    fail=True; break
                edge_owner[kk]=pi_new
            if fail: break
        if fail: break
    # Recompute triangle edge types from actual neighbors (handles mixed contexts like
    # 3.3.4.3.4 where a single canonical sequence is insufficient). Triangles are congruent,
    # so rotating their sequence to match actual neighbors is always valid.
    if not fail:
        for p in polys:
            if p.n != 3: continue
            newseq=[]
            for i in range(3):
                a=p.v[i]; b=p.v[(i+1)%3]
                rk=(norm(b), norm(a))
                if rk in edge_owner:
                    newseq.append(polys[edge_owner[rk]].n)
                else:
                    newseq.append(p.seq[i])
            # only accept if exactly one triangle neighbor and two square neighbors (type B)
            if newseq.count(3)==1 and newseq.count(4)==2:
                p.seq=newseq

    # consistency check all edges (skip patch boundary edges; only check paired edges)
    if fail:
        return None, 'edge conflict'
    for pi,p in enumerate(polys):
        for i in range(p.n):
            a=p.v[i]; b=p.v[(i+1)%p.n]
            rkey=(norm(b), norm(a))
            if rkey not in edge_owner:
                continue  # patch boundary
            opi=edge_owner[rkey]
            op=polys[opi]
            j=None
            for jj in range(op.n):
                if norm(op.v[jj])==norm(b) and norm(op.v[(jj+1)%op.n])==norm(a):
                    j=jj; break
            if j is None:
                if debug: print('final mismatch', pi, i, 'j',j,'p.n',p.n,'p.seq',p.seq[i], 'op',other,'op.n',op.n,'op.seq',op.seq[j])
                if keep_on_mismatch:
                    return polys, 'type mismatch'
                return None, 'type mismatch'
            if not (p.seq[i]==op.n and op.seq[j]==p.n):
                # Ignore label mismatches on boundary-adjacent polygons; interior labels
                # are repaired after BFS and boundary labels are irrelevant for the
                # fundamental domain.
                p_interior = all((norm(p.v[e]), norm(p.v[(e+1)%p.n]))[::-1] in edge_owner for e in range(p.n))
                op_interior = all((norm(op.v[e]), norm(op.v[(e+1)%op.n]))[::-1] in edge_owner for e in range(op.n))
                if p_interior and op_interior:
                    if debug: print('final mismatch', pi, i, 'j',j,'p.n',p.n,'p.seq',p.seq[i], 'op',other,'op.n',op.n,'op.seq',op.seq[j])
                    if keep_on_mismatch:
                        return polys, 'type mismatch'
                    return None, 'type mismatch'
    return polys, None

# ---- period detection (vertex set) ----
def find_period(polys, min_comp=0.2, max_comp=60.0):
    verts={}
    for p in polys:
        for v in p.v:
            verts[norm(v)] = v
    pts=list(verts.values())
    keyset=set(verts.keys())
    central=[complex(k[0]/SCALE,k[1]/SCALE) for k in keyset if abs(k[0]/SCALE)<2.5 and abs(k[1]/SCALE)<2.5]
    if len(central)<5:
        central=pts
    # candidate vectors from differences of central vertices only (sufficient for smallest periods)
    cands=[]
    for a in central:
        for b in central:
            dx=b.real-a.real; dy=b.imag-a.imag
            if dx*dx+dy*dy > 1e-6:
                cands.append(complex(dx,dy))
    # deduplicate and sort by length
    cands=sorted(set((round(v.real,3), round(v.imag,3), v) for v in cands), key=lambda t: abs(t[2]))
    def is_trans(v, tol=0.02):
        for z in central:
            target=complex(z.real+v.real, z.imag+v.imag)
            found=False
            for w in pts:
                if abs(w.real-target.real)<tol and abs(w.imag-target.imag)<tol:
                    found=True; break
            if not found:
                return False
        return True
    # Collect passing vectors.
    passed=[]
    for _,_,v in cands:
        if abs(v.real)<min_comp and abs(v.imag)<min_comp: continue
        if abs(v.real)>max_comp or abs(v.imag)>max_comp: continue
        if is_trans(v):
            passed.append(v)
    if len(passed)<2:
        return None,None
    # Choose W: smallest passing vector with |y| ~ 0 and x > 0.
    W=None
    for v in passed:
        if abs(v.imag)<0.02 and v.real>0:
            W=v; break
    if W is None:
        W=passed[0]
    # Choose H: smallest |H| among passing vectors with cross(W,H)>0, y>0, x>=0 (slanted lattice).
    H=None
    for v in passed:
        cross=W.real*v.imag-W.imag*v.real
        if cross>1e-6 and v.imag>1e-6 and v.real>=-1e-6:
            if H is None or abs(v)<abs(H):
                H=v
    if H is None:
        for v in passed:
            cross=W.real*v.imag-W.imag*v.real
            if cross>1e-6:
                if H is None or abs(v)<abs(H):
                    H=v
    if H is None:
        return None,None
    return W,H

# ---- build fundamental domain ----
def build_domain(cfg,L,seqs,max_polys=1500):
    polys,err=generate(cfg,L,seqs,max_polys=max_polys)
    if polys is None:
        print('generate fail', cfg, err); return None
    print('generated', cfg, 'polys', len(polys), Counter(p.n for p in polys))
    W,H=find_period(polys)
    if W is None:
        print('no period', cfg); return None
    print('period', cfg, 'W', W, 'H', H, 'area', abs(W.real*H.imag-W.imag*H.real))
    # rotate whole patch so W is along +x (axis-aligned wrap like hex); H then has positive y.
    if W.real*H.imag-W.imag*H.real < 0:
        W,H=H,W
    ang=-math.atan2(W.imag, W.real)
    c=math.cos(ang); s=math.sin(ang)
    def rot(z):
        return complex(z.real*c-z.imag*s, z.real*s+z.imag*c)
    for p in polys:
        p.v=[rot(v) for v in p.v]
    Wr=rot(W); Hr=rot(H)
    # After rotation Wr is along +x; cross > 0 implies Hr.imag > 0.
    W=complex(abs(W),0.0)
    H=complex(Hr.real, Hr.imag)
    print('rotated W,H', W, H)
    # collect base cells by centroid modulo W,H (solve in lattice coordinates)
    cells=[]
    seen=[]
    det=W.real*H.imag-W.imag*H.real
    for p in polys:
        cx=sum(p.v)/p.n
        # solve cx = base + u*W + v*H; u,v in [0,1)
        u=(cx.real*H.imag - cx.imag*H.real)/det
        v=(W.real*cx.imag - W.imag*cx.real)/det
        u-=math.floor(u); v-=math.floor(v)
        if u>0.98: u=0.0
        if v>0.98: v=0.0
        # merge near duplicates (numerical drift across patch)
        dup=False
        for su,sv,_ in seen:
            if abs(su-u)<0.02 and abs(sv-v)<0.02:
                dup=True; break
        if dup: continue
        seen.append((u,v,None))
        # base center = u*W + v*H
        bcx=complex(u)*W + complex(v)*H
        shift=bcx-cx
        vv=[z+shift for z in p.v]
        cells.append({'n':p.n,'seq':p.seq,'cx':bcx.real,'cy':bcx.imag,'v':[[z.real,z.imag] for z in vv]})
    print('base cells', len(cells), Counter(c['n'] for c in cells))
    return {'cfg':cfg,'L':L,'W':[W.real,W.imag],'H':[H.real,H.imag],'cells':cells}

if __name__=='__main__':
    def run():
        configs=[
            ([3,3,4,3,4], math.sqrt(6/(2+math.sqrt(3)))),
            ([3,4,6,4], math.sqrt(6/(3+2*math.sqrt(3)))),
            ([3,6,3,6], math.sqrt(math.sqrt(3)/2)),
            ([3,12,12], 1/math.sqrt(2+7*math.sqrt(3)/6)),
            ([4,6,12], 1/math.sqrt(1.5+math.sqrt(3))),
            ([4,8,8], 2-math.sqrt(2)),
        ]
        for cfg,L in configs:
            print('===',cfg)
            for n in sorted(set(cfg)):
                seqs=valid_seqs(cfg,n)
                print(' n',n,'seqs',len(seqs),seqs[:5])
            per={n:valid_seqs(cfg,n) for n in set(cfg)}
            best=None
            def try_comb(types, chosen):
                nonlocal best
                if best is not None: return
                if not types:
                    d=build_domain(cfg,L,chosen)
                    if d: best=d
                    return
                n=types[0]
                for s in per[n]:
                    cc=dict(chosen); cc[n]=s
                    try_comb(types[1:], cc)
            try_comb(sorted(set(cfg)), {})
            if best:
                print('BEST', json.dumps(best)[:500])
            else:
                print('NO COMBINATION WORKS')
    run()
