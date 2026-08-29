#!/usr/bin/env python3
# collect12.py — 按用户前提：30°顶点跨两块聚合12个三角。用正确物理三角唯一键(顶点frozenset)收集。
import json, math
from collections import defaultdict
SP=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))['laves_4612']
W=SP['W'][0];hx=SP['H'][0];hy=SP['H'][1];cells=SP['cells'];B=len(cells)
SHORT=math.sqrt(2.0/math.sqrt(3.0));LONG=SHORT*math.sqrt(3.0);HYP=2*SHORT
def vk(p):return (round(p[0],5),round(p[1],5))
def ang_of(v,p):
    Ls=[]
    for k in range(3):
        u=v[k];w=v[(k+1)%3]
        if vk(u)==vk(p) or vk(w)==vk(p):Ls.append(round(math.hypot(w[0]-u[0],w[1]-u[1]),4))
    Ls=sorted(set(Ls))
    if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-LONG)<1e-3:return 90
    if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-HYP)<1e-3:return 30
    if abs(Ls[0]-LONG)<1e-3 and abs(Ls[1]-HYP)<1e-3:return 60
    return 0
# 让块铺到 dc∈[-1,2], dr∈[-1,2] 以确保跨块; 每个物理三角用其"顶点集合"(世界坐标)唯一判断
K=3
# 物理三角唯一: 先收集所有实例, 按三角形三顶点(归一化)集合去重
tris=defaultdict(list)   # frozenset(vk each world vert) -> [(b,dc,dr)]
for b in range(B):
    for dc in range(-K,K+1):
        for dr in range(-K,K+1):
            ox=dc*W+dr*hx;oy=dr*hy
            v=[(x+ox,y+oy) for x,y in cells[b]['v']]
            key=frozenset(vk(p) for p in v)
            tris[key].append((b,dc,dr))
# 每物理三角, 对其3顶点各记该角
v30=defaultdict(set)  # vk(p) -> set(tri_key) 该点30°
for key,insts in tris.items():
    b,dc,dr=insts[0]
    ox=dc*W+dr*hx;oy=dr*hy
    v=[(x+ox,y+oy) for x,y in cells[b]['v']]
    for p in v:
        if ang_of(v,p)==30: v30[vk(p)].add(key)
from collections import Counter
dist=Counter(len(s) for s in v30.values())
print('物理30°顶点 三角数分布:',dict(sorted(dist.items())))
n12=[(p,s) for p,s in v30.items() if len(s)==12]
print('30°=12 物理顶点:',len(n12))
for p,s in n12[:10]:
    print('  v=%s'%(p,))
