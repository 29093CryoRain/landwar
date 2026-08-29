import json, math, collections

def load():
    d = {}
    for f in ('data/tiling_specs_arch.json', 'data/tiling_specs_laves.json'):
        d.update(json.load(open(f, encoding='utf-8')))
    return d

d = load()

def analyze(name):
    spec = d[name]
    W = spec['W']; H = spec['H']
    Wv = (W[0], W[1]); Hv = (H[0], H[1])
    cells = spec['cells']
    verts = {}
    for i in range(-2, 3):
        for j in range(-2, 3):
            ox = i * Wv[0] + j * Hv[0]
            oy = i * Wv[1] + j * Hv[1]
            for c in cells:
                for p in c['v']:
                    px = p[0] + ox; py = p[1] + oy
                    verts[(round(px, 5), round(py, 5))] = verts.get((round(px, 5), round(py, 5)), 0) + 1
    return verts, Wv, Hv, cells

for name in list(d.keys()):
    verts, Wv, Hv, cells = analyze(name)
    vcount = collections.Counter()
    for (x, y), n in verts.items():
        if -0.01 <= x <= Wv[0] + 0.01 and -0.01 <= y <= Hv[1] + 0.01:
            vcount[n] += 1
    total = sum(vcount.values())
    print(f"{name}: block {len(cells)} cells, Wv=({Wv[0]:.4f},{Wv[1]:.4f}) Hv=({Hv[0]:.4f},{Hv[1]:.4f})")
    print(f"   valency histogram in block bounds: {dict(sorted(vcount.items()))}")

if __name__ == '__main__':
    pass
