#!/usr/bin/env python3
# analyze_angles.py — 统计每个世界顶点上 30/60/90 角各出现几次，识别哪些顶点是"综合顶点"。
import json, math
from collections import defaultdict
SP=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))['laves_4612']
W=SP['W'][0];hx=SP['H'][0];hy=SP['H'][1];cells=SP['cells']
B=len(cells)
SHORT=math.sqrt(2.0/math.sqrt(3.0)); LONG=SHORT*math.sqrt(3.0); HYP=2*SHORT
def vk(p):return (round(p[0],6),round(p[1],6))
# collect per vertex: count of 30,60,90 angles + which cells
vangles=defaultdict(lambda:{30:0,60:0,90:0})
vcell=defaultdict(set)
for b in range(B):
    v=cells[b]['v']
    edges=[]
    for k in range(3):
        u=v[k];w=v[(k+1)%3];edges.append((vk(u),vk(w),round(math.hypot(w[0]-u[0],w[1]-u[1]),4)))
    for k in range(3):
        p=v[k];Ls=sorted(set(l for (u,w,l) in edges if u==vk(p) or w==vk(p)))
        if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-LONG)<1e-3: a=90
        elif abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-HYP)<1e-3: a=30
        elif abs(Ls[0]-LONG)<1e-3 and abs(Ls[1]-HYP)<1e-3: a=60
        else: a=0
        vangles[vk(p)][a]+=1; vcell[vk(p)].add(b)
# 识别"综合顶点"(角数=多,属laves节点): 打印所有顶点角组成
print("=== 顶点角组合 (只列角数>=3的综合顶点) ===")
for v,ang in sorted(vangles.items(),key=lambda kv:-sum(kv[1].values())):
    tot=sum(ang.values())
    s30,s60,s90=ang[30],ang[60],ang[90]
    if tot>=2:  # 至少2格共享=内部顶点
        print('v=%s  30°:×%d  60°:×%d  90°:×%d  (cells %s)'%(
            v,s30,s60,s90, sorted(vcell[v])))
