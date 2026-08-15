import math, cmath
L = math.sqrt(6/(2+math.sqrt(3)))
DIRS = [0,60,150,210,300]
TRI_TRI = 0  # direction at index 0 is the tri-tri edge

def key(z):
    return (round(z.real,6), round(z.imag,6))

def norm_angle(a):
    a = math.fmod(a, 2*math.pi)
    if a < 0: a += 2*math.pi
    return a

start_angle = math.radians(0)
verts = {key(0j): start_angle}
pos = {key(0j): 0j}
q=[0j]
while q:
    v=q.pop(0)
    phi = verts[key(v)]
    for idx,ddeg in enumerate(DIRS):
        d = phi + math.radians(ddeg)
        w = v + L * complex(math.cos(d), math.sin(d))
        # determine phi_w
        if ddeg == TRI_TRI:
            phi_w = norm_angle(phi + math.pi)
        else:
            # choose beta: smallest beta such that phi_w+beta = d+pi
            # beta in {60,150,210,300}; phi_w = d+pi-beta
            betas = [60,150,210,300]
            phi_w = None
            for b in betas:
                cand = norm_angle(d + math.pi - math.radians(b))
                # choose candidate that keeps tri-tri direction at a boundary? For now pick first.
                phi_w = cand
                break
        k=key(w)
        if k in verts:
            if abs(norm_angle(vert(k)) - phi_w) > 1e-6:
                # try other betas
                pass
        if k not in verts:
            verts[k]=phi_w; pos[k]=w; q.append(w)
print('verts', len(verts))
