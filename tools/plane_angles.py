#!/usr/bin/env python3
# plane_angles.py — 3x3 平面分析：找 12×30°、12×60°、12×90° 的各顶点 + 所属格(含块偏移)。
import json, math
from collections import defaultdict
SP=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))['laves_4612']
W=SP['W'][0];hx=SP['H'][0];hy=SP['H'][1];cells=SP['cells'];B=len(cells)
SHORT=math.sqrt(2.0/math.sqrt(3.0)); LONG=SHORT*math.sqrt(3.0); HYP=2*SHORT
def vk(p):return (round(p[0],6),round(p[1],6))
K=3
# (b,dc,dr) plane cells with world verts
plane=[]
for b in range(B):
    for dc in range(-K,K+1):
        for dr in range(-K,K+1):
            ox=dc*W+dr*hx;oy=dr*hy
            plane.append((b,dc,dr,[(x+ox,y+oy) for x,y in cells[b]['v']]))
vangles=defaultdict(lambda:{':30':0,':60':0,':90':0})
vcell=defaultdict(set)
for (b,dc,dr,v) in plane:
    edges=[]
    for k in range(3):
        u=v[k];w=v[(k+1)%3];edges.append((vk(u),vk(w),round(math.hypot(w[0]-u[0],w[1]-u[1]),4)))
    for k in range(3):
        p=v[k];Ls=sorted(set(l for (u,w,l) in edges if u==vk(p) or w==vk(p)))
        if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-LONG)<1e-3: a=90
        elif abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-HYP)<1e-3: a=30
        else: a=60
        vangles[vk(p)][{30:':30',60:':60',90:':90'}[a]]+=1
        vcell[vk(p)].add((b,dc,dr))
# 找3D等效顶点(世界唯一): 顶点在世界坐标唯一; 看其角数
print('=== 共顶点三角(角数聚合,只要出现6/12/18等)===')
for v,cnt in sorted(vangles.items()):
    s30=cnt[':30'];s60=cnt[':60'];s90=cnt[':90']
    if s30+s60+s90>=3:
        # normalize world vertex to base block (wrap by W,H)
        nx=round(v[0]%W,6);ny=round(v[1]%hy,6)
        print('v=%-18s=>norm(%.3f,%.3f) 30×%d 60×%d 90×%d cells=%s'%(
            v,nx,ny,s30,s60,s90, sorted([c[0] for c in vcell[v]])))
