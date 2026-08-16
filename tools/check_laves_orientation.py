#!/usr/bin/env python3
"""检查各 Laves 密铺在 JSON 原始朝向 与 loadTable 旋转90°后的朝向，对照文档方向规范。"""
import json
import math

TOL = 1e-6


def load(name, file, rotate):
    d = json.load(open(file, encoding='utf-8'))
    j = d[name]
    old_wx = j['W'][0]
    old_hy = j['H'][1]
    cells = []
    for c in j['cells']:
        if rotate:
            nc = old_hy - c['cy']
            ny = c['cx']
            v = [[old_hy - p[1], p[0]] for p in c['v']]
            cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
        else:
            cells.append({'n': c['n'], 'cx': c['cx'], 'cy': c['cy'], 'v': [list(p) for p in c['v']]})
    return {'wx': (old_hy if rotate else old_wx), 'hx': 0.0,
            'hy': (old_wx if rotate else old_hy)}, cells


def edge_stats(cells):
    """每个格子的边：长度、方向角（度，相对 +x）。"""
    stats = []
    for c in cells:
        for k in range(c['n']):
            A = c['v'][k]
            Bv = c['v'][(k + 1) % c['n']]
            dx, dy = Bv[0] - A[0], Bv[1] - A[1]
            ln = math.sqrt(dx * dx + dy * dy)
            ang = math.degrees(math.atan2(dy, dx)) % 180.0  # 无向
            if ang > 90.0:
                ang -= 180.0
            stats.append((ln, ang, dx, dy))
    return stats


def main():
    for name in ['laves_3636', 'laves_31212', 'laves_4612', 'laves_488', 'laves_3464']:
        print('=' * 60)
        for rotate in [False, True]:
            tab, cells = load(name, 'data/tiling_specs_laves.json', rotate)
            stats = edge_stats(cells)
            # 按长度分类（3 种边长）
            by_len = {}
            for ln, ang, dx, dy in stats:
                key = round(ln, 4)
                found = None
                for kk in by_len:
                    if abs(kk - key) < 0.01:
                        found = kk
                        break
                by_len.setdefault(found if found is not None else key, []).append((ln, ang, dx, dy))
            print('%s %s:' % (name, 'ROT90' if rotate else 'JSON '))
            for ln0, arr in sorted(by_len.items()):
                angs = sorted(set(round(a, 1) for _, a, _, _ in arr))
                print('   len≈%.4f (%d 条) 方向角=%s' % (ln0, len(arr), angs))


if __name__ == '__main__':
    main()
