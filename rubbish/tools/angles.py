#!/usr/bin/env python3
# angles.py — 重建 laves_4612 几何：每个三角胞标出 30°/60°/90° 角顶点位置（世界坐标），便于按文档
# "某角共顶点"来正确构形。
import json, math
from collections import defaultdict
SP=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))['laves_4612']
W=SP['W'][0];hx=SP['H'][0];hy=SP['H'][1];cells=SP['cells']
B=len(cells)
SHORT=math.sqrt(2.0/math.sqrt(3.0)); LONG=SHORT*math.sqrt(3.0); HYP=2*SHORT
def vk(p):return (round(p[0],6),round(p[1],6))
# 每个三角胞的 3 顶点 + 各自角度
# 由顶点到圆心: 90°=直角边交点; 30°=短边与斜边交点; 60°=长边与斜边交点
# 判定: 顶点v 处两邻边长度 (a=b? ) 
for b in range(B):
    v=cells[b]['v']
    edges=[]
    for k in range(3):
        u=v[k];w=v[(k+1)%3]
        edges.append((vk(u),vk(w),round(math.hypot(w[0]-u[0],w[1]-u[1]),4)))
    # 每顶点的两条边
    ang={}
    for k in range(3):
        p=v[k];e=[(u,w,L) for (u,w,L) in edges if u==vk(p) or w==vk(p)]
        Ls=sorted(set(l for (_,__,l) in e))
        # 两邻边: 90°的两边是 short+long; 30°的两边是 short+hyp; 60°的两边是 long+hyp
        if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-LONG)<1e-3: ang[vk(p)]=90
        elif abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-HYP)<1e-3: ang[vk(p)]=30
        elif abs(Ls[0]-LONG)<1e-3 and abs(Ls[1]-HYP)<1e-3: ang[vk(p)]=60
        else: ang[vk(p)]='?'
    print('c%2d  right(v90)=%s  v30=%s  v60=%s'%(b,
        [p for p,a in ang.items() if a==90],
        [p for p,a in ang.items() if a==30],
        [p for p,a in ang.items() if a==60]))
