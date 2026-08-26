#!/usr/bin/env python3
"""gen_table_domain_params.py — 离线重算表驱动密铺的"比例保持映射"参数 (s,t,p,q,Ra,Rb).

算法两阶段（见 .docs/地图尺寸比例映射.md）：
  · 算法一（本工具，离线）：遍历 s·t=B 的正因式分解（s=每块列数、t=每块行数）与互质 (p,q)，
    强约束 max(Ra,Rb) ≤ K_MAX_GAP（用户要求输入间距≤16）。评价**真实地图纵横比**：
    对若干测试输入 (a,b) 模拟 chooseTableDomain → (cols,rows) → 用数据真实几何算
    worldWidth/worldHeight（轴对齐 = 未剪切，斜周期 = 平行四边形 AABB），
    偏差 |ln(纵横比) − ln(a/b)| 越小越好；平局取 max(Ra,Rb) 小、再取块更方(s,t)。
  · 算法二（在线，C++ chooseTableDomain）：a'=ceil(a/Ra)·Ra、b'=ceil(b/Rb)·Rb；
      c=p·a'/q、d=q·b'/p；cols=c/s、rows=d/t。

用法:
    python tools/gen_table_domain_params.py            # 打印全部密铺参数 + 约束校验
    python tools/gen_table_domain_params.py --emit     # 打印可直接替换 tableDomainParams 的 C++ 表
"""
import json, math, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARCH = os.path.join(ROOT, 'data/tiling_specs_arch.json')
LAVES = os.path.join(ROOT, 'data/tiling_specs_laves.json')

K_MAX_GAP = 16   # 用户约束：最大输入间距（Ra/Rb 上界）
P_MAX = 60       # p,q 搜索上界
TEST_INPUTS = [(105, 95), (150, 80), (80, 150), (120, 120), (60, 200), (200, 60)]


def gcd(a, b):
    while b:
        a, b = b, a % b
    return a


def load_all():
    d = {}
    d.update(json.load(open(ARCH, encoding='utf-8')))
    d.update(json.load(open(LAVES, encoding='utf-8')))
    return d


def world_bounds(d, cols, rows):
    """真实世界 AABB 宽/高：x = base.v.x + c*wx + r*hx、y = base.v.y + c*wy + r*hy。
    四角 (c,r)∈{0,cols-1}x{0,rows-1} 偏移极值 + 基础格 AABB 跨幅。"""
    W = d['W']; H = d['H']
    wx, wy, hx, hy = W[0], W[1], H[0], H[1]
    xs = [p[0] for c in d['cells'] for p in c['v']]
    ys = [p[1] for c in d['cells'] for p in c['v']]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    cm, rm = max(0, cols - 1), max(0, rows - 1)
    xsh = [0.0, cm * wx, rm * hx, cm * wx + rm * hx]
    ysh = [0.0, cm * wy, rm * hy, cm * wy + rm * hy]
    ww = (xmax - xmin) + (max(xsh) - min(xsh))
    wh = (ymax - ymin) + (max(ysh) - min(ysh))
    return ww, wh


def simulate(d, s, t, p, q, Ra, Rb, a, b):
    """在线算法二 → (cols,rows) → 实际纵横比。返回 (aspect, 偏差)。"""
    a2 = max(1, ((a + Ra - 1) // Ra) * Ra)
    b2 = max(1, ((b + Rb - 1) // Rb) * Rb)
    c = p * a2 // q
    dd = q * b2 // p
    if c % s or dd % t:
        return None, None
    cols, rows = c // s, dd // t
    if cols < 1 or rows < 1:
        return None, None
    ww, wh = world_bounds(d, cols, rows)
    if wh <= 0 or ww <= 0:
        return None, None
    return ww / wh, abs(math.log(ww / wh) - math.log(a / b))


def factor_pairs(B):
    """s·t=B 的全部正因式分解。B≥4 且存在两因子≥2 时，排除退化 1×B / B×1 块。"""
    pairs = [(s, B // s) for s in range(1, B + 1) if B % s == 0]
    has_2d = any(s >= 2 and t >= 2 for s, t in pairs)
    if has_2d:
        pairs = [(s, t) for s, t in pairs if s >= 2 and t >= 2]
    return pairs


def search(name, d):
    B = len(d['cells'])
    W = d['W']; H = d['H']
    wx = W[0]
    hy = abs(H[1])
    best = None
    for s, t in factor_pairs(B):
        u = wx / s
        v = hy / t
        if u <= 0 or v <= 0:
            continue
        target = math.sqrt(v / u)
        for p in range(1, P_MAX + 1):
            for q in range(1, P_MAX + 1):
                if gcd(p, q) != 1:
                    continue
                Ra = q * s // gcd(s, p)
                Rb = p * t // gcd(t, q)
                if max(Ra, Rb) > K_MAX_GAP:
                    continue
                err = abs(math.log(p / q) - 0.5 * math.log(v / u))
                gap = max(Ra, Rb)
                cand = (err, gap, abs(s - t), s, t, p, q, Ra, Rb)
                if best is None or cand < best:
                    best = cand
    return best


def main():
    data = load_all()
    tilings = ['arch_33336', 'arch_33434', 'arch_3464', 'arch_3636', 'arch_31212', 'arch_4612',
               'arch_488', 'laves_3636', 'laves_31212', 'laves_4612', 'laves_488', 'laves_33434',
               'laves_33336', 'laves_3464']
    rows = {}
    for name in tilings:
        d = data.get(name)
        if not d:
            print('%-14s  (no data)' % name)
            continue
        best = search(name, d)
        if best is None:
            print('%-14s  NO SOLUTION within gap<=%d' % (name, K_MAX_GAP))
            continue
        worst, gap, sq, s, t, p, q, Ra, Rb = best
        rows[name] = (s, t, p, q, Ra, Rb)
        print('%-14s B=%2d -> s=%2d t=%2d p=%2d q=%2d Ra=%2d Rb=%2d gap=%2d err=%.4f'
              % (name, len(d['cells']), s, t, p, q, Ra, Rb, gap, worst))

    if '--emit' in sys.argv:
        print('\n/* ---- 替换 Tiling.cpp tableDomainParams 的 kTable ---- */')
        order = ['arch_33336', 'arch_33434', 'arch_3464', 'arch_3636', 'arch_31212', 'arch_4612',
                 'arch_488', 'laves_3636', 'laves_31212', 'laves_4612', 'laves_488',
                 'laves_33434', 'laves_33336', 'laves_3464']
        for n in order:
            r = rows.get(n)
            if not r:
                print('        /* %s */ {}, {},' % n)
                continue
            s, t, p, q, Ra, Rb = r
            print('        /* %s */ {%d, %d, %d, %d, %d, %d},' % (n, s, t, p, q, Ra, Rb))

    if '--verify' in sys.argv:
        print('\n==== 约束校验（对当前 kTable 参数）====')
        ok_all = True
        for name, r in rows.items():
            s, t, p, q, Ra, Rb = r
            d = data[name]
            B = len(d['cells'])
            wx = d['W'][0]
            hy = abs(d['H'][1])
            checks = []
            # ① s*t == B
            checks.append(('s*t==B', s * t == B))
            # ⑥ gcd(p,q)==1
            checks.append(('gcd(p,q)=1', gcd(p, q) == 1))
            # ⑦ Ra,Rb 公式一致
            checks.append(('Ra=qs/gcd(s,p)', Ra == q * s // gcd(s, p)))
            checks.append(('Rb=pt/gcd(t,q)', Rb == p * t // gcd(t, q)))
            # ⑤ gap≤16
            checks.append(('gap<=%d' % K_MAX_GAP, max(Ra, Rb) <= K_MAX_GAP))
            # ②③ 对测试输入：c|s、d|t、保守 a*b=c*d、cols/rows 整数
            conserv = True
            aspect_ok = True
            for a, b in TEST_INPUTS:
                a2 = max(1, ((a + Ra - 1) // Ra) * Ra)
                b2 = max(1, ((b + Rb - 1) // Rb) * Rb)
                c = p * a2 // q
                dd = q * b2 // p
                if c % s or dd % t or c * dd != a2 * b2 or c // s < 1 or dd // t < 1:
                    conserv = False
                # ④ aspect: (c*u)/(d*v) vs a/b（u=wx/s、v=hy/t）
                u = wx / s
                v = hy / t
                aspect = (c * u) / (dd * v)
                dev = abs(math.log(aspect) - math.log(a2 / b2))
                if dev > 0.30:   # 容忍 ~30% 纵横比偏差（受 gap≤16 限制）
                    aspect_ok = False
            checks.append(('②③ 乘积+倍数+cols/rows', conserv))
            checks.append(('④ 纵横比≈输入', aspect_ok))
            bad = [n for n, v2 in checks if not v2]
            ok_all = ok_all and not bad
            print('%-14s  %s' % (name, 'OK' if not bad else 'FAIL ' + ','.join(bad)))
        print('\nAll-ok: %s' % ok_all)


if __name__ == '__main__':
    main()
