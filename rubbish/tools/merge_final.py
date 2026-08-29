#!/usr/bin/env python3
"""merge_final.py — replace arch_33434 / arch_33336 / laves_33336 in the spec files with the
corrected (direction-fixed) data. Preserves all other entries and laves_33434."""
import json, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FIN = {
    'arch_33434': json.load(open(os.path.join(ROOT, 'tools/blocks/final_arch_33434.json'))),
    'arch_33336': json.load(open(os.path.join(ROOT, 'tools/blocks/final_arch_33336.json'))),
    'laves_33336': json.load(open(os.path.join(ROOT, 'tools/blocks/final_laves_33336.json'))),
}

arch = json.load(open(os.path.join(ROOT, 'data/tiling_specs_arch.json')))
laves = json.load(open(os.path.join(ROOT, 'data/tiling_specs_laves.json')))

# add cfg for arch entries (json spec has cfg field for the snub ones)
for name in ['arch_33434', 'arch_33336']:
    if name in arch:
        if 'cfg' in arch[name] and 'cfg' not in FIN[name]:
            FIN[name]['cfg'] = arch[name]['cfg']
arch['arch_33434'] = FIN['arch_33434']
arch['arch_33336'] = FIN['arch_33336']
laves['laves_33336'] = FIN['laves_33336']

json.dump(arch, open(os.path.join(ROOT, 'data/tiling_specs_arch.json'), 'w'), indent=1)
json.dump(laves, open(os.path.join(ROOT, 'data/tiling_specs_laves.json'), 'w'), indent=1)

print('arch entries:', sorted(arch.keys()))
print('laves entries:', sorted(laves.keys()))
print('arch_33434 W/H:', arch['arch_33434']['W'], arch['arch_33434']['H'], 'B=%d' % len(arch['arch_33434']['cells']))
print('arch_33336 W/H:', arch['arch_33336']['W'], arch['arch_33336']['H'], 'B=%d' % len(arch['arch_33336']['cells']))
print('laves_33336 W/H:', laves['laves_33336']['W'], laves['laves_33336']['H'], 'B=%d' % len(laves['laves_33336']['cells']))
print('laves_33434 preserved:', laves['laves_33434']['W'], laves['laves_33434']['H'], 'B=%d' % len(laves['laves_33434']['cells']))
