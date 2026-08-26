#!/usr/bin/env python3
# gen_shapes.py — 用正确拓扑(30°=12格跨块满周, 60°=6格, 90°=4格)生成 laves_4612 城市形状。
# 关键：以"归一化到块[0,W)x[0,H) 的顶点"为桶键——跨块的同一物理顶点落入同桶，聚齐全满周三角。
# 输出每个满周顶点对应的格集(以 base+dc+dr 世界表示)，供后续转城市形状 store 偏移。
import json, math
from collections import defaultdict
SP=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))['laves_4612']
W=SP['W'][0];hx=SP['H'][0];hy=SP['H'][1];cells=SP['cells'];B=len(cells)
SHORT=math.sqrt(2.0/math.sqrt(3.0)); LONG=SHORT*math.sqrt(3.0); HYP=2*SHORT
def vk(p): return (round(p[0],6),round(p[1],6))
def ang_of(entryverts, p):
    Ls=[]
    for k in range(3):
        u=entryverts[k]; w=entryverts[(k+1)%3]
        if vk(u)==vk(p) or vk(w)==vk(p):
            Ls.append(round(math.hypot(w[0]-u[0],w[1]-u[1]),4))
    Ls=sorted(set(Ls))
    if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-LONG)<1e-3: return 90
    if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-HYP)<1e-3: return 30
    if abs(Ls[0]-LONG)<1e-3 and abs(Ls[1]-HYP)<1e-3: return 60
    return 0
def norm(p):
    x=p[0]%W; y=p[1]%hy
    if abs(x-W)<1e-5: x=0.0
    if abs(y-hy)<1e-5: y=0.0
    return (round(x,6),round(y,6))
# 收集: 归一化顶点 -> list of (entryverts, angle, base, dc, dr)  [每个三角贡献其"未在邻块重复"的角]
# 顶点角: 三角在顶点p处的角,只计一次。三角胞有3顶点各1角。跨块物理同点会被多个块实例贡献 —
# 但我们只需给定区域(如 dc,dr ∈[-1,1])的三角实例, 同一物理三角只出现一次即可。
K=2
# 物理三角去重: 用质心归一化到块[0,W)x[0,H) 为唯一键; 每个物理三角只计一次
trikey=defaultdict(list)  # norm(centroid) -> [物理三角 (b,dc,dr) ]
for b in range(B):
    for dc in range(-K,K+1):
        for dr in range(-K,K+1):
            ox=dc*W+dr*hx; oy=dr*hy
            verts=[(x+ox,y+oy) for x,y in cells[b]['v']]
            cx=sum(x for x,_ in verts)/3; cy=sum(y for _,y in verts)/3
            trikey[norm((cx,cy))].append((b,dc,dr))
seen=set()
bucket=defaultdict(list)  # norm(p) -> [(b,dc,dr,ang)]
for b in range(B):
    for dc in range(-K,K+1):
        for dr in range(-K,K+1):
            ox=dc*W+dr*hx; oy=dr*hy
            verts=[(x+ox,y+oy) for x,y in cells[b]['v']]
            cx=sum(x for x,_ in verts)/3; cy=sum(y for _,y in verts)/3
            key=norm((cx,cy))
            if key in seen: continue
            seen.add(key)
            for p in verts:
                a=ang_of(verts,p)
                bucket[norm(p)].append((b,dc,dr,a))
# 找满周30°顶点(该桶内 30° 角三角数==12 的; 聚合跨块)
print('=== 30°满周顶点(桶30°角三角数==12) ===')
cnt=0
for p,items in bucket.items():
    tri30=set((b,dc,dr) for (b,dc,dr,a) in items if a==30)
    if len(tri30)==12:
        cnt+=1
        if cnt<=12:
            print('  np=%s  30°tri=%d  bases=%s'%(p,len(tri30),sorted(set(b for (b,_,_,_) in tri30))))
print('count=',cnt)
