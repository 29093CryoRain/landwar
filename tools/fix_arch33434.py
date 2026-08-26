#!/usr/bin/env python3
"""fix_arch33434.py — regenerate arch_33434 at theta=+45 (B=12, axis-aligned 3.464x3.464),
the orientation the user confirmed correct (cand_arch_33434_t45_0.png / t-45). Leaves the two
33336 tilings untouched."""
import json, math, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import tools.gen_snub as g

THETA = 45.0


def main():
    r = g.build('arch_33434', THETA, True)
    if r is None:
        print('no axis period at theta=%s' % THETA)
        return
    cells = r['cells']
    nW = r['W'][0]
    nH = r['H'][1]
    # adjacency (axis-aligned: wy=0)
    ok, tot, miss = g.adjacency(cells, nW, 0.0, nH)
    spec = {'cfg': arch_cfg(), 'W': [round(nW, 12), 0.0], 'H': [0.0, round(nH, 12)],
            'cells': [{'n': c['n'], 'cx': round(c['cx'], 12), 'cy': round(c['cy'], 12),
                       'v': [[round(p[0], 12), round(p[1], 12)] for p in c['v']]} for c in cells]}
    json.dump(spec, open(os.path.join(ROOT, 'tools/blocks/fixed_arch_33434.json'), 'w'), indent=1)
    print('arch_33434 @ theta=%g : B=%d W=%s H=%s adj=%d/%d' %
          (THETA, len(cells), spec['W'], spec['H'], ok, tot))
    # merge into data file
    arch = json.load(open(os.path.join(ROOT, 'data/tiling_specs_arch.json')))
    arch['arch_33434'] = spec
    json.dump(arch, open(os.path.join(ROOT, 'data/tiling_specs_arch.json'), 'w'), indent=1)
    print('written arch_33434 B=%d W=%s' % (len(spec['cells']), spec['W']))


def arch_cfg():
    arch = json.load(open(os.path.join(ROOT, 'data/tiling_specs_arch.json')))
    return arch['arch_33434'].get('cfg')


if __name__ == '__main__':
    main()
