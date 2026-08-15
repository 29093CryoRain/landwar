#!/usr/bin/env python3
# Export working Archimedean tiling fundamental domains to data/tiling_specs_arch.json.
# Uses gen_arch2.py propagation. Configs not yet supported: [3,3,4,3,4], [3,3,3,3,6].
import importlib.util, math, json, os

spec = importlib.util.spec_from_file_location('g', 'tools/gen_arch2.py')
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

CONFIGS = [
    ([3,4,6,4], math.sqrt(6/(3+2*math.sqrt(3))), 'arch_3464'),
    ([3,6,3,6], math.sqrt(math.sqrt(3)/2), 'arch_3636'),
    ([3,12,12], 1/math.sqrt(2+7*math.sqrt(3)/6), 'arch_31212'),
    ([4,6,12], 1/math.sqrt(1.5+math.sqrt(3)), 'arch_4612'),
    ([4,8,8], 2-math.sqrt(2), 'arch_488'),
]

def main():
    out = {}
    for cfg, L, name in CONFIGS:
        per = {n: g.valid_seqs(cfg, n) for n in set(cfg)}
        # For these configs, each type has exactly one valid sequence; use it.
        seqs = {n: per[n][0] for n in per}
        d = g.build_domain(cfg, L, seqs, max_polys=1500)
        if d is None:
            print('FAILED', name)
            continue
        # Round tiny floats and drop seq (not needed by C++).
        for c in d['cells']:
            c.pop('seq', None)
            c['cx'] = round(c['cx'], 12)
            c['cy'] = round(c['cy'], 12)
            c['v'] = [[round(x, 12), round(y, 12)] for x, y in c['v']]
        d['W'] = [round(x, 12) for x in d['W']]
        d['H'] = [round(x, 12) for x in d['H']]
        out[name] = {'cfg': d['cfg'], 'L': d['L'], 'W': d['W'], 'H': d['H'], 'cells': d['cells']}
        print('OK', name, 'W', d['W'], 'H', d['H'], 'cells', len(d['cells']))
    path = os.path.join('data', 'tiling_specs_arch.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
    print('wrote', path)

if __name__ == '__main__':
    main()
