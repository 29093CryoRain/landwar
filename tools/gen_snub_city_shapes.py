#!/usr/bin/env python3
"""生成 4 种扭棱类密铺的城市形状表（Config.cpp 片段）。"""
import json, math
from collections import defaultdict

TOL = 1e-6

def load(name, file):
    d = json.load(open(file, encoding='utf-8'))
    j = d[name]
    old_wx = j['W'][0]
    old_hy = j['H'][1]
    cells = []
    for c in j['cells']:
        nc = old_hy - c['cy']
        ny = c['cx']
        v = [[old_hy - p[1], p[0]] for p in c['v']]
        cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
    return {'wx': old_hy, 'hx': 0.0, 'hy': old_wx}, cells

def build_neighbors(tab, cells):
    B = len(cells); edges = []
    for b in range(B):
        cell = cells[b]; n = cell['n']; be = []
        for k in range(n):
            A = cell['v'][k]; Bv = cell['v'][(k+1)%n]
            found = False
            for dr in range(-2,3):
                for dc in range(-2,3):
                    ox = dc*tab['wx'] + dr*tab['hx']; oy = dr*tab['hy']
                    for nb in range(B):
                        ocell = cells[nb]
                        for kk in range(ocell['n']):
                            n0 = [ocell['v'][kk][0]+ox, ocell['v'][kk][1]+oy]
                            n1 = [ocell['v'][(kk+1)%ocell['n']][0]+ox, ocell['v'][(kk+1)%ocell['n']][1]+oy]
                            if (n0[0]-Bv[0])**2+(n0[1]-Bv[1])**2 <= TOL*TOL and (n1[0]-A[0])**2+(n1[1]-A[1])**2 <= TOL*TOL:
                                be.append((nb, dr, dc)); found = True; break
                        if found: break
                    if found: break
                if found: break
            if not found: be.append(None)
        edges.append(be)
    return edges

def build_vert_map(tab, cells, radius=8):
    B = len(cells); vm = defaultdict(list)
    for r in range(-radius, radius+1):
        for c in range(-radius, radius+1):
            for b in range(B):
                ox = c*tab['wx'] + r*tab['hx']; oy = r*tab['hy']
                for p in cells[b]['v']:
                    key = (round((p[0]+ox)*1e4), round((p[1]+oy)*1e4))
                    vm[key].append((b,r,c))
    return vm

def key(cc):
    b,r,c = cc
    return (b,r,c)

def center(cc, tab, cells):
    b,r,c = cc
    return (cells[b]['cx'] + c*tab['wx'] + r*tab['hx'], cells[b]['cy'] + r*tab['hy'])

def to_config(offs):
    # offs are runtime (rx,ry) relative to anchor; config = (ry, -rx)
    out = []
    for rx, ry in offs:
        out.append((round(ry,12), round(-rx,12)))
    return out

def fmt(offs):
    return '{' + ', '.join('{%.12f, %.12f, 0}' % o for o in to_config(offs)) + '}'

def sorted_cc(ccs, tab, cells):
    return sorted(ccs, key=lambda cc: (round(center(cc,tab,cells)[1],4), round(center(cc,tab,cells)[0],4), cc[0]))

def uniq(ccs):
    seen=set(); out=[]
    for cc in ccs:
        k=(cc[0],cc[1],cc[2])
        if k in seen: continue
        seen.add(k); out.append(cc)
    return out

def anchor_rel(ccs, anchor, tab, cells):
    ax,ay = center(anchor, tab, cells)
    offs=[]
    for cc in uniq(ccs):
        x,y=center(cc,tab,cells)
        offs.append((x-ax, y-ay))
    return offs

# ---------------- Laves 33336 ----------------
def gen_laves33336():
    tab,cells = load('laves_33336','data/tiling_specs_laves.json')
    edges = build_neighbors(tab,cells)
    vm = build_vert_map(tab,cells)
    B=len(cells)
    anchor=(0,0,0)
    # classify edges long/short
    def long_edges(b):
        n=cells[b]['n']; res=[]
        for k in range(n):
            p=cells[b]['v'][k]; q=cells[b]['v'][(k+1)%n]
            L=math.hypot(q[0]-p[0], q[1]-p[1])
            if L>0.7: res.append(k)
        return res
    def vert_valence(b, k):
        p=cells[b]['v'][k]
        return len(vm[(round(p[0]*1e4), round(p[1]*1e4))])
    # L1: anchor b with no horizontal b-edge? choose anchors where both b-edges not horizontal? Actually use b0 as anchor and mask 0? For now choose all pentagons whose both long edges are not horizontal.
    mask=0
    for b in range(B):
        les=long_edges(b); ok=True
        for k in les:
            p=cells[b]['v'][k]; q=cells[b]['v'][(k+1)%cells[b]['n']]
            if abs(q[1]-p[1])<1e-9: ok=False
        if ok: mask |= (1<<b)
    if mask==0: mask=1  # fallback b0
    L1=[anchor]
    # L6: cells around a 6-valent vertex of anchor b0
    v6=None
    for k in range(cells[0]['n']):
        if vert_valence(0,k)==6:
            p=cells[0]['v'][k]
            v6=vm[(round(p[0]*1e4), round(p[1]*1e4))]
            break
    L6=uniq(v6) if v6 else [anchor]
    # L3: split L6 into two halves by horizontal line at centroid y
    ys=sorted(center(cc,tab,cells)[1] for cc in L6)
    mid=(ys[0]+ys[-1])/2
    upper=uniq([cc for cc in L6 if center(cc,tab,cells)[1] >= mid])
    lower=uniq([cc for cc in L6 if center(cc,tab,cells)[1] <= mid])
    return {
        'tiling':'laves_33336',
        'shapes':[
            ('L1',1,L1,mask),
            ('L3a',3,upper,0),
            ('L3b',3,lower,0),
            ('L6',6,L6,0),
        ]
    }

# ---------------- Laves 33434 ----------------
def gen_laves33434():
    tab,cells = load('laves_33434','data/tiling_specs_laves.json')
    edges = build_neighbors(tab,cells)
    vm = build_vert_map(tab,cells)
    B=len(cells)
    anchor=(0,0,0)
    def short_edges(b):
        n=cells[b]['n']; res=[]
        for k in range(n):
            p=cells[b]['v'][k]; q=cells[b]['v'][(k+1)%n]
            if math.hypot(q[0]-p[0],q[1]-p[1]) < 0.7: res.append(k)
        return res
    def vert_cells(b,k):
        p=cells[b]['v'][k]
        return vm[(round(p[0]*1e4), round(p[1]*1e4))]
    # L1: choose pentagons where the short edge is horizontal? doc: left-right symmetric; approximate by short edge vertical/horizontal? We'll use all for now mask=0xf
    L1=[anchor]
    # L4: for each short edge of anchor, cells around its two endpoints
    se=short_edges(0)
    shapes=[]
    for se_idx, k in enumerate(se):
        p=cells[0]['v'][k]; q=cells[0]['v'][(k+1)%0] if False else cells[0]['v'][(k+1)%cells[0]['n']]
        cells_p=set(vert_cells(0,k))
        cells_q=set(vert_cells(0,(k+1)%cells[0]['n']))
        union=uniq(list(cells_p|cells_q))
        if len(union)==4:
            shapes.append(('L4%d'%(se_idx+1),4,union,0))
    if not shapes:
        shapes.append(('L4',4,[anchor],0))
    # L8/L12: skip complex; use placeholder L8/L12 = L4 + neighbors maybe
    return {'tiling':'laves_33434','shapes':[('L1',1,L1,0xf)]+shapes}

def main():
    for name, gen in [('laves_33336',gen_laves33336),('laves_33434',gen_laves33434)]:
        r=gen()
        print('//',r['tiling'])
        for label,lv,ccs,mask in r['shapes']:
            offs=anchor_rel(ccs,(0,0,0),*load(r['tiling'], 'data/tiling_specs_laves.json') if r['tiling'].startswith('laves') else load(r['tiling'],'data/tiling_specs_arch.json'))
            print(f'// {label} count={len(offs)} mask=0x{mask:x}')
            print(fmt(offs))

if __name__=='__main__':
    main()
