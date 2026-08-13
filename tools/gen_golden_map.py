#!/usr/bin/env python3
"""gen_golden_map.py — 复刻新地形基图解析（海陆确定性 + P13 城市等级/形状 + 山骰 + 邻海修正 + 首都），
生成 C++ 单测黄金数据。

用法（在项目根 new_project_landwar/ 下执行）:
    python tools/gen_golden_map.py [--seed 42] [--map data/map_bigIslands.bmp]
                                   [--out tests/data/golden_map.json]
                                   [--alpha 1.0] [--beta 0.5]

与 C++ 端（src/world/Map.cpp + src/core/Random.cpp）逐位对齐的前提:
  1. Rng::get(n) = 阈值拒绝法（无取模偏差）；Rng::chance(p) = next()/2^32 < p；
     Rng::unit() = next()/2^32（单次抽取）。
  2. MT19937 以 init_genrand(seed) 初始化。
  3. BMP 逐字节解析：54 字节头；每行 width*3 字节 + 填充到 4 对齐（105 宽 → 1 字节）；
     j 行 = 图像底部行（自底向上）。
  4. **P13 两遍解析**：第一遍读全图通道 → 定海/陆（确定性，不掷骰）；
     第二遍按原迭代序（y 外 x 内）：海格不掷；陆格若非城市占用格 →
       掷城概率 rng.chance(ramp(G))，通过则 sampleCityLevel（幂律，1 次 unit()）后尝试放置
       （锚点须可产城格 + 全基建格陆地 + 无重叠），放置失败回退 1 级；无论占用与否恒掷山骰
       rng.chance(ramp(R))。ramp(x) = 0 if x<=128 else min(1, (x-128)/127)。
     占用格（已被既有形状覆盖）跳过城概率（RNG 位置确定性）。
  5. 等级采样 s = alpha + beta：P(L=1)=1-2^-s; P(L=2)=2^-s-4^-s; P(L=4)=4^-s-6^-s;
     P(L=6)=6^-s-9^-s; P(L=9)=9^-s。等级表 {1,2,4,6,9}，形状
     {1×1, 1×2, 2×2, 2×3, 3×3}（列×行；思路 9.1 表格记作行×列 = {1×1, 2×1, 2×2, 3×2, 3×3}，
     与 Config::city 内置一致）。
  6. 邻海修正：山格 8 邻域（含对角）有海 → 置普通陆（纯循环，无 RNG）。
  7. **P6 RNG 分离（双流）**：地图骰子（山/城/等级）用 mapRng = MT(mapSeed)、首都用
     mainRng = MT(seed)（各自独立实例）。mapSeed 默认 = seed，可用 --map-seed 指定。
  8. 首都：8 个；每 attempt 消耗 get(width-1) + get(height-1)；与已放置首都欧氏距离 < 28 重抽。
     P13：锚点已是城市（形状）→ 该城即首都；否则注册 1 级城。

输出 JSON（nlohmann 可精确回读）:
  seed / width / height / land[height][width] / mountain[height][width] /
  city[height][width]（格 ∈ 某城 = true，对应 C++ cityId>=0）/
  citylevel[height][width]（每格所属城等级；非城 = 0，黄金测试校验等级）/
  capitals[[x,y]×8] / total_cities（= 城市注册表大小，非格子数）
"""

import argparse
import json
import math
import os

MAP_X = 105
MAP_Y = 95
SEA_MIN = 32
PROB_FLOOR = 128
PROB_SCALE = 127
MIN_DIST = 28
MAX_ATTEMPTS = 1000000

# P13 等级 → 形状（宽=列数, 高=行数）。与 Config::City 内置 levels/shapes 一致
# （思路 9.1 表格记作"行数×列数"：2 级=2行×1列 → (1,2)；6 级=3行×2列 → (2,3)）。
LEVELS = [1, 2, 4, 6, 9]
SHAPES = {1: (1, 1), 2: (1, 2), 4: (2, 2), 6: (2, 3), 9: (3, 3)}


class MT19937:
    """与 std::mt19937 一致：init_genrand 初始化 + 标准 tempering。get/chance/unit 复刻 C++ Rng
    （阈值拒绝法无取模偏差 + next()/2^32 均匀）。"""

    def __init__(self, seed):
        self.mt = [0] * 624
        self.index = 624
        self.mt[0] = seed & 0xFFFFFFFF
        for i in range(1, 624):
            self.mt[i] = (1812433253 * (self.mt[i - 1] ^ (self.mt[i - 1] >> 30)) + i) & 0xFFFFFFFF

    def _twist(self):
        for i in range(624):
            y = (self.mt[i] & 0x80000000) | (self.mt[(i + 1) % 624] & 0x7FFFFFFF)
            self.mt[i] = self.mt[(i + 397) % 624] ^ (y >> 1)
            if y & 1:
                self.mt[i] ^= 2567483615  # 0x9908B0DF
        self.index = 0

    def next(self):
        if self.index >= 624:
            self._twist()
        y = self.mt[self.index]
        self.index += 1
        y ^= y >> 11
        y ^= (y << 7) & 2636928640   # 0x9D2C5680
        y ^= (y << 15) & 4022730752  # 0xEFC60000
        y ^= y >> 18
        return y & 0xFFFFFFFF

    def get(self, n):
        """与 C++ Rng::get 一致：阈值拒绝法（无取模偏差）。"""
        nplus = n + 1
        limit = ((1 << 32) // nplus) * nplus
        while True:
            v = self.next()
            if v < limit:
                return v % nplus

    def chance(self, p):
        """与 C++ Rng::chance 一致：next()/2^32 < p。"""
        return self.next() / 4294967296.0 < p

    def unit(self):
        """与 C++ Rng::unit 一致：next()/2^32 ∈ [0,1)（单次抽取）。"""
        return self.next() / 4294967296.0


def ramp(x):
    """概率 ramp：p = clamp((x - 128) / 127, 0, 1)。与 C++ rampProb 一致。"""
    if x <= PROB_FLOOR:
        return 0.0
    return min(1.0, (x - PROB_FLOOR) / PROB_SCALE)


def sample_level(rng, s):
    """P13 等级采样（幂律）：与 C++ sampleCityLevel 一致（1 次 unit()）。
    P(L=levels[i]) = levels[i]^-s - levels[i+1]^-s（末级 = levels[N-1]^-s）。"""
    u = rng.unit()
    cum = 0.0
    for i, lev in enumerate(LEVELS):
        cur = lev ** (-s)
        nxt = LEVELS[i + 1] ** (-s) if i + 1 < len(LEVELS) else 0.0
        cum += cur - nxt
        if u < cum:
            return lev
    return LEVELS[-1]  # 浮点兜底（累计 ≈ 1）


def can_place(land, cityid, cityallowed, level, x, y):
    """P13 放置检查：与 C++ Map::canPlaceCity 一致。"""
    w, h = SHAPES[level]
    if x < 0 or y < 0 or x + w > MAP_X or y + h > MAP_Y:
        return False
    if not cityallowed[y][x]:
        return False  # 锚点须可产城格
    for dy in range(h):
        for dx in range(w):
            if not land[y + dy][x + dx]:
                return False
            if cityid[y + dy][x + dx] != -1:
                return False
    return True


def place_city(land, cityid, cityallowed, citylevel, level, x, y, next_id):
    """P13 放置：尝试 level，失败回退 1 级（与 C++ addCity 后置检查一致）。返回 (cid, next_id)。
    citylevel 记录每格所属城市的等级（非城 = 0，黄金测试校验等级用）。"""
    for lev in (level, 1):
        if can_place(land, cityid, cityallowed, lev, x, y):
            w, h = SHAPES[lev]
            for dy in range(h):
                for dx in range(w):
                    cityid[y + dy][x + dx] = next_id
                    citylevel[y + dy][x + dx] = lev
            return next_id, next_id + 1
    return -1, next_id


def parse_map(path, rng, s):
    """地形基图两遍解析 + 邻海修正。返回 land/mountain/cityid/cityallowed/citylevel。"""
    land = [[0] * MAP_X for _ in range(MAP_Y)]
    mountain = [[0] * MAP_X for _ in range(MAP_Y)]
    cityid = [[-1] * MAP_X for _ in range(MAP_Y)]  # 每格所属城市 id；-1 = 非城市
    cityallowed = [[0] * MAP_X for _ in range(MAP_Y)]
    citylevel = [[0] * MAP_X for _ in range(MAP_Y)]  # 每格所属城市等级；非城 = 0
    next_id = 0

    # 第一遍：读全图通道 → 确定海/陆（确定性，不掷骰）并**置全图 land**。P13 两遍解析——
    # 必须先行置 land，否则第二遍放置多格形状时检查的"未来格"（同行右侧/下方行）land 仍是
    # 0 默认，放置恒失败回退 1 级（与 C++ loadFromBmp 2026-08-07 修复对齐）。
    channels = {}
    with open(path, 'rb') as f:
        header = f.read(54)
        assert len(header) == 54, "header truncated"
        rowsize = (MAP_X * 3 + 3) // 4 * 4
        for j in range(MAP_Y):
            row = f.read(rowsize)
            assert len(row) == rowsize, f"row {j} truncated"
            for i in range(MAP_X):
                b, g, r = row[i * 3], row[i * 3 + 1], row[i * 3 + 2]
                channels[(j, i)] = (r, g, b)
                land[j][i] = 0 if min(r, g, b) < SEA_MIN else 1  # 第一遍即定全图海/陆

    # 第二遍：原迭代序（y 外 x 内）。海格收尾；陆格掷城骰（占用格跳过）+ 恒掷山骰。
    # land 已在第一遍置好（海格 0 / 陆格 1），本遍只掷骰与放置。
    for j in range(MAP_Y):
        for i in range(MAP_X):
            r, g, b = channels[(j, i)]
            if land[j][i] == 0:
                cityid[j][i] = -1
                continue
            cityallowed[j][i] = 1 if ramp(g) > 0 else 0
            if cityid[j][i] != -1:
                # 已被既有城市形状占用：不掷城概率（RNG 位置确定性），仍掷山骰。
                mountain[j][i] = 1 if rng.chance(ramp(r)) else 0
                continue
            if rng.chance(ramp(g)):
                lev = sample_level(rng, s)
                cid, next_id = place_city(land, cityid, cityallowed, citylevel, lev, i, j, next_id)
            mountain[j][i] = 1 if rng.chance(ramp(r)) else 0

    # 邻海修正（与 C++ correctMountainCoast 一致；只读 land，结果与顺序无关）。
    for j in range(MAP_Y):
        for i in range(MAP_X):
            if not mountain[j][i]:
                continue
            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    if di == 0 and dj == 0:
                        continue
                    ni, nj = i + di, j + dj
                    if 0 <= ni < MAP_X and 0 <= nj < MAP_Y and land[nj][ni] == 0:
                        mountain[j][i] = 0
                        break
                else:
                    continue
                break
    return land, mountain, cityid, cityallowed, citylevel, next_id


def place_capitals(rng, land, cityallowed, cityid, citylevel, next_id):
    """放置 8 个首都。P5 改版：优先落在可产城格（基图允许成城）；每放一个首都前判断是否还有
    "可用可产城格"（未被占用且距已放首都 >= MIN_DIST）——有则只收可产城格，无则放宽到任意陆地。
    与 C++ Map::placeCapitals 逐位一致（重试同一 slot）。
    P13：锚点格已是城市（loadFromBmp 放置的形状）→ 该城即首都（不新建）；否则注册 1 级城。
    返回 (capitals, next_id)。"""
    cx = [-9961] * 8
    cy = [-9961] * 8
    attempts = 0
    for i in range(8):
        cx[i] = -9961
        cy[i] = -9961
        any_usable = False
        for j in range(MAP_Y):
            if any_usable:
                break
            for k in range(MAP_X):
                if not land[j][k] or not cityallowed[j][k]:
                    continue
                far = all(math.sqrt((k - cx[q]) ** 2 + (j - cy[q]) ** 2) >= MIN_DIST
                          for q in range(i))
                if far:
                    any_usable = True
                    break
        while True:
            rx = rng.get(MAP_X - 1)
            ry = rng.get(MAP_Y - 1)
            attempts += 1
            if attempts > MAX_ATTEMPTS:
                raise RuntimeError("capital retry exhausted")
            if not land[ry][rx]:
                continue  # 同一 slot 重试
            if any_usable and not cityallowed[ry][rx]:
                continue  # 还有可产城格可用 → 只收它
            too_close = any(math.sqrt((rx - cx[q]) ** 2 + (ry - cy[q]) ** 2) < MIN_DIST
                            for q in range(i))
            if too_close:
                continue  # 同一 slot 重试
            cx[i] = rx
            cy[i] = ry
            break
    capitals = list(zip(cx, cy))
    # P13：锚点已是城市 → 不新建；否则注册 1 级城（形状 1×1，无放置检查，锚点即首格）。
    for x, y in capitals:
        if cityid[y][x] != -1:
            continue  # 已有城市（多格形状）→ 该城即首都
        cityid[y][x] = next_id
        citylevel[y][x] = 1
        next_id += 1
    return capitals, next_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=42)
    parser.add_argument('--map-seed', type=int, default=None, help='地图种子（默认 = seed）')
    parser.add_argument('--map', default='data/map_bigIslands.bmp')
    parser.add_argument('--out', default='tests/data/golden_map.json')
    parser.add_argument('--alpha', type=float, default=1.0, help='levelIncomeExponent（默认 1.0）')
    parser.add_argument('--beta', type=float, default=0.5, help='levelRankExponent（默认 0.5）')
    args = parser.parse_args()

    s = args.alpha + args.beta  # 等级幂律指数（= rankExponent）
    map_seed = args.seed if args.map_seed is None else args.map_seed
    # P6 RNG 分离：地图骰子（山/城/等级）与首都用各自独立的 MT 流。
    map_rng = MT19937(map_seed)
    land, mountain, cityid, cityallowed, citylevel, next_id = parse_map(args.map, map_rng, s)
    main_rng = MT19937(args.seed)
    capitals, next_id = place_capitals(main_rng, land, cityallowed, cityid, citylevel, next_id)

    # P13 统计：城市注册表大小 = 已分配的 city id 数（非格子数；所有城均占陆地格，无海上清理）。
    total_cities = next_id

    golden = {
        'seed': args.seed,
        'width': MAP_X,
        'height': MAP_Y,
        'land': [[bool(v) for v in row] for row in land],
        'mountain': [[bool(v) for v in row] for row in mountain],
        'city': [[v >= 0 for v in row] for row in cityid],  # 格 ∈ 某城（对应 C++ cityId>=0）
        'citylevel': [[v for v in row] for row in citylevel],  # 每格所属城等级（非城 0）
        'cityallowed': [[bool(v) for v in row] for row in cityallowed],
        'capitals': [[x, y] for x, y in capitals],
        'total_cities': total_cities,
    }

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(golden, f, indent=1, separators=(',', ': '))

    land_count = sum(sum(row) for row in land)
    mtn_count = sum(sum(row) for row in mountain)
    print(f'seed={args.seed} land={land_count}/{MAP_X * MAP_Y} mountain={mtn_count} '
          f'cities={total_cities} capitals={capitals}')


if __name__ == '__main__':
    main()
