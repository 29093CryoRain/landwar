#!/usr/bin/env python3
# correct_shapes.py — 用正确几何(按角类型围绕顶点BFS)生成 laves_4612 城市形状。
# 算法: 给定一个"角类型"和"顶点"(世界),收集所有该顶点处恰有此角类型的三角胞,得到形状格集(带块偏移)。
# 然后: 选参考base, 存储偏移=(世界偏移) 反解 (-dy,dx), 逐 anchor 验证。
import json, math
from collections import defaultdict
SP=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))['laves_4612']
W=SP['W'][0];hx=SP['H'][0];hy=SP['H'][1];cells=SP['cells'];B=len(cells)
SHORT=math.sqrt(2.0/math.sqrt(3.0)); LONG=SHORT*math.sqrt(3.0); HYP=2*SHORT
def vk(p):return (round(p[0],6),round(p[1],6))
def cen(i):
    v=cells[i]['v'];return (sum(x[0] for x in v)/3,sum(x[1] for x in v)/3)
K=4
plane=[]
for b in range(B):
    for dc in range(-K,K+1):
        for dr in range(-K,K+1):
            ox=dc*W+dr*hx;oy=dr*hy
            plane.append({'b':b,'dc':dc,'dr':dr,'v':[(x+ox,y+oy) for x,y in cells[b]['v']]})
def ang_of(entry, p):
    v=entry['v'];edges=[]
    for k in range(3):
        u=v[k];w=v[(k+1)%3];edges.append((vk(u),vk(w),round(math.hypot(w[0]-u[0],w[1]-u[1]),4)))
    Ls=sorted(set(l for (u,w,l) in edges if u==vk(p) or w==vk(p)))
    if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-LONG)<1e-3: return 90
    if abs(Ls[0]-SHORT)<1e-3 and abs(Ls[1]-HYP)<1e-3: return 30
    if abs(Ls[0]-LONG)<1e-3 and abs(Ls[1]-HYP)<1e-3: return 60
    return 0
# 建 顶点-> (entries where that vertex has angle A)
vmap=defaultdict(list) # vk(p) -> [(entry, angle)]
for e in plane:
    for p in e['v']:
        vmap[vk(p)].append((e,ang_of(e,p)))
def at_type(vw, A):
    # 找最接近 vw 的既有顶点, 返回该顶点全部 entries 中 angle==A 的
    # 简化: vw 直接是某顶点的世界坐标(vk 匹配于 plane 内). 但 vw 是 base-block 的角.
    return vw
# 指定"世界顶点"(跨块归一)收集: 直接给世界坐标(在 plane 内唯一)
def collect(vw, A):
    return [(e['b'],e['dc'],e['dr']) for (e,a) in vmap.get(vk(vw),[]) if a==A]
# 各形状的顶点(世界)与角类型:
cand={
 # L6: 6×60° 满周, 顶点(0,3.224): cells本块 {0,3,4,12,15,16}
 'L6_60deg_v0': ((0.0,3.22371),60,'L6_60deg_v0'),
 # L12a: 12×30°跨块 = 30°对角. 顶点(1.861,4.298) 有 6×30°本块{0,1,4,6,7,10}, 邻块补6.
 'L12_30deg_v4298': ((1.86121,4.29828),30,'L12_30deg_v4298'),
 'L12_30deg_v2149': ((1.86121,2.14914),30,'L12_30deg_v2149'),
}
for kw,(vw,A,name) in cand.items():
    got=collect(vw,A)
    # 归一化块偏移到相对本块(以 b=dc=dr=0 为参考)
    print('%s: vertex=%s angle=%d -> %d cells'%(name,vw,A,len(got)))
    for (b,dc,dr) in sorted(got):
        print('    b=%2d dc=%+d dr=%+d'%(b,dc,dr))
