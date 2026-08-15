import math, cmath
L = math.sqrt(6/(2+math.sqrt(3)))
r3 = L/math.sqrt(3)
r4 = L/math.sqrt(2)
angles = [30,105,180,255,330]
pts = []
for a in angles:
    rad = math.radians(a)
    if a in (30,180,330):
        r = r3
    else:
        r = r4
    pts.append(complex(r*math.cos(rad), r*math.sin(rad)))
print('L',L)
for p in pts: print(p)
sides=[]
for i in range(5):
    d=abs(pts[(i+1)%5]-pts[i]); sides.append(d); print('side',i,d)
def area(pts):
    s=0
    for i in range(5):
        a=pts[i]; b=pts[(i+1)%5]
        s+=a.real*b.imag-a.imag*b.real
    return abs(s)/2
A=area(pts)
print('area',A,'scale to 1',1/math.sqrt(A))
