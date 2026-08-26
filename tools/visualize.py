#!/usr/bin/env python3
# visualize.py — 打印 4 个综合顶点处的格子,以及每个候选形状的特征(质心/角点),用于判断正确几何。
import json, math
from collections import defaultdict
SP=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))['laves_4612']
W=SP['W'][0];hx=SP['H'][0];hy=SP['H'][1];cells=SP['cells'];B=len(cells)
def cen(i):
    v=cells[i]['v'];return (sum(x[0] for x in v)/3,sum(x[1] for x in v)/3)
groups={
 'T6x30(v4.298)':[0,1,4,6,7,10],        # 顶点(1.861,4.298) 6×30°
 'T6x60(v0,3.224)':[0,3,4,12,15,16],    # (0,3.224) 6×60°
 'T6x60(v1.861,6.447)':[1,2,5,7,8,11],  # (1.861,6.447) 6×60°
 'T6x60(v3.722,3.224)':[6,9,10,19,20,23],# (3.722,3.224) 6×60°
 'T6x60(v1.861,0)':[13,14,17,18,21,22], # (1.861,0) 6×60°
 'T4x90(v1.861,3.224)':[0,6,12,20],      # 90° 组
 'T4x90(v0.931,4.836)':[1,3,4,5],
 'T4x90(v2.792,4.836)':[7,9,10,11],
 'T4x90(v0.931,1.612)':[13,15,16,17],
 'T4x90(v2.792,1.612)':[19,21,22,23],
}
for nm,gs in groups.items():
    cs=[cen(i) for i in gs]
    mx=sum(c[0] for c in cs)/len(cs);my=sum(c[1] for c in cs)/len(cs)
    xs=[c[0] for c in cs];ys=[c[1] for c in cs]
    print('%s : cells%s'%(nm,gs))
    for i in gs:
        print('    c%d ctr=(%.3f,%.3f)'%(i,*cen(i)))
    print('    bbox x[%.3f,%.3f] y[%.3f,%.3f]  centroid=(%.3f,%.3f)'%(min(xs),max(xs),min(ys),max(ys),mx,my))
    print()
