#!/usr/bin/env python3
# build_laves4612_new.py — 按用户明示构造生成 laves_4612 新方案，直接写出 spec。
# 三角形：短直角边=1, 长直角边=sqrt3, 斜边=2。
# 大矩形: 左上(0,3) 右下(2*sqrt3,-3)。线 x=sqrt3, y=0 分4小矩形。
# 左上小矩形: 左下(0,0)-右上(sqrt3,3)主对角; A(0,2)-B(sqrt3,1); A-右上; B-左下 => 6三角。
# 变换: TL+(sqrt3,-3)=右下; x取负+2sqrt3=右上; y取负=左下。
# 归一化 m=sqrt(2/sqrt3) 使每三角面积=1。所有 cell 绕向统一为逆时针。
# 引擎约定基础块格中心落在 [0,W]x[0,H]，本构造 y∈[-H/2,H/2] 故整体平移 +H/2。
import json, math
S3=math.sqrt(3.0)
m=math.sqrt(2.0/S3)
def sc(p): return (p[0]*m, p[1]*m)

BL=(0.0,0.0); BR=(S3,0.0); TL=(0.0,3.0); TR=(S3,3.0)
A=(0.0,2.0); B=(S3,1.0); MID=(S3/2.0,1.5)
TLtris=[
 [BL,B,BR], [TR,B,MID], [A,TL,TR], [BL,A,MID], [BL,MID,B], [TR,MID,A],
]

def face_ccw(t):
    s=sum(t[k][0]*t[(k+1)%3][1]-t[(k+1)%3][0]*t[k][1] for k in range(3))
    return list(t) if s>0 else list(reversed(t))

def tf(faces, f):
    return [face_ccw([sc(f(p)) for p in t]) for t in faces]

def place(tris):
    out=[]
    for t in tris:
        vv=[[round(x,12),round(y,12)] for x,y in t]
        cx=sum(p[0] for p in vv)/len(vv); cy=sum(p[1] for p in vv)/len(vv)+m*3.0
        vv=[[round(x,12),round(y+m*3.0,12)] for x,y in vv]
        out.append({'n':3,'cx':round(cx,12),'cy':round(cy,12),'v':vv})
    return out

cells=(place(tf(TLtris, lambda p:(p[0],p[1])))
     + place(tf(TLtris, lambda p:(-p[0]+2*S3,p[1])))
     + place(tf(TLtris, lambda p:(p[0],-p[1])))
     + place(tf(TLtris, lambda p:(p[0]+S3,p[1]-3.0))))

W=2*S3*m; H=6.0*m
cur=json.load(open('data/tiling_specs_laves.json',encoding='utf-8'))
cur['laves_4612']={'W':[round(W,12),0.0],'H':[0.0,round(H,12)],'cells':cells}
json.dump(cur,open('data/tiling_specs_laves.json','w',encoding='utf-8'),indent=1)
print("wrote laves_4612: W=%.9f H=%.9f cells=%d"%(W,H,len(cells)))
