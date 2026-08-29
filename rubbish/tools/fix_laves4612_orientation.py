#!/usr/bin/env python3
"""修正 laves_4612 spec 朝向：把 JSON 数据旋转 90°（同 loadTable 变换 x'=hy-y, y'=x，W/H 交换），
使 loadTable 旋转 90° 后运行时斜边竖直（符合开发思路.txt 方向规范"存在竖直方向的斜边"）。

仅 4612 需要：其余 4 个 Laves 密铺 JSON 是"中间朝向"，旋转 90° 后满足规范；
4612 的 JSON 已满足规范，旋转 90° 后反而不满足，故预旋转 JSON 使其旋转后满足。
"""
import json

TOL = 1e-9


def rot_cell(c, old_hy):
    nc = old_hy - c['cy']
    ny = c['cx']
    v = []
    for p in c['v']:
        v.append([round(old_hy - p[1], 12), round(p[0], 12)])
    return {'n': c['n'], 'cx': round(nc, 12), 'cy': round(ny, 12), 'v': v}


def main():
    path = 'data/tiling_specs_laves.json'
    with open(path, encoding='utf-8') as f:
        d = json.load(f)
    j = d['laves_4612']
    old_wx = j['W'][0]
    old_hy = j['H'][1]
    new_cells = [rot_cell(c, old_hy) for c in j['cells']]
    j['W'] = [round(old_hy, 12), 0.0]
    j['H'] = [0.0, round(old_wx, 12)]
    j['cells'] = new_cells
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(d, f, indent=1)
    print('rotated laves_4612: W=%s H=%s cells=%d' % (j['W'], j['H'], len(new_cells)))


if __name__ == '__main__':
    main()
