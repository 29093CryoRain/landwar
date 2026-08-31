# -*- coding: utf-8 -*-
"""gen_city_shapes_json.py — 从 src/core/Config.cpp 的 initDefaultCity 内置表提取
全部密铺（square/hex/tri + 半正/Laves）城市等级/形状表，生成 data/city_shapes.jsonc。

- anchorBaseMask 数值（0x.. / 十进制 / constexpr 表达式）转为 '0'/'1' 串
 （MSB 在左，bit b = 允许 b 号基础格作锚），C++ 读入时按二进制还原为 uint32。
- P1.2 起 square/hex/tri 也输出；cells 统一为世界偏移。

用法：
    python tools/gen_city_shapes_json.py [源文件] [输出文件]
默认：src/core/Config.cpp → data/city_shapes.jsonc
注意：2026-08-26 表外置后，当前 Config.cpp 只含 square/hex/tri 内置表；若要重新生成
完整 arch/laves 表，须指向仍含 initDefaultCity 完整表的旧源（如 rubbish 中的备份）。
"""
import json
import re
import sys

SRC_DEFAULT = "src/core/Config.cpp"
OUT_DEFAULT = "data/city_shapes.jsonc"

COMMENT_LINES = [
    "全部密铺（square/hex/tri/半正/Laves）城市等级/形状表（2026-08-27 P1.2 重写）。",
    "由 tools/gen_city_shapes_json.py 生成，改表请重跑该工具。",
    "baseGroups（每密铺，手动填写）：几何完全一致的基础格归为一组，组名任意。示例：",
    "  \"baseGroups\": { \"axis_square\": [2, 8], \"hex_flat\": [0, 3] }",
    "anchorBases（形状的锚限制）：组名或基础格编号的数组，可混用；缺省 = 不限锚。",
    "  示例：\"anchorBases\": [\"axis_square\"] 或 [2, 8] 或 [\"axis_square\", 5]。",
    "朝向参考基准：mask 非 0 取最小编号基础格，否则基础格 b=0。",
    "cells 每项 = [dx, dy]（相对锚格中心的世界偏移；square/hex/tri 与半正/Laves 统一）。",
    "三角正/反朝向由锚格奇偶在运行时镜像处理。",
    "各密铺下 _byEdgeCount 键 = 按边数自动分组的建议数据（下划线键程序不读取），供填写参考。",
]


def extract_braced(text, i):
    """text[i] 必须是 '{'，返回 (花括号内内容, 闭括号后下一个索引)。"""
    assert text[i] == "{"
    depth = 0
    for j in range(i, len(text)):
        c = text[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:j], j + 1
    raise ValueError("unbalanced braces")


def split_top(s, sep=","):
    """按顶层分隔符切分（忽略 (), {}, [] 嵌套内的 sep）。"""
    parts, depth, cur = [], 0, []
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        parts.append("".join(cur))
    return [p.strip() for p in parts if p.strip()]


def parse_scalar_list(inner):
    """'{a, b, c}' 内容 → 数值列表。"""
    out = []
    for tok in split_top(inner):
        tok = re.sub(r"[fF]\b", "", tok.strip())
        out.append(float(tok))
    return out


def parse_int_list(inner):
    return [int(float(t.strip())) for t in split_top(inner)]


def parse_cells(arg):
    """'{{dx,dy}, …}' → [[dx,dy], …]（旧三元素取前两项，orient 已废弃）。"""
    inner, _ = extract_braced(arg, arg.index("{"))
    cells = []
    for item in split_top(inner):
        ci, _ = extract_braced(item, item.index("{"))
        vals = parse_scalar_list(ci)
        cells.append(vals[:2])
    return cells


def expand_sq(w, h):
    return [[float(dx), float(dy)] for dy in range(h) for dx in range(w)]


def eval_mask(expr, consts):
    expr = expr.strip()
    if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", expr):
        return int(expr, 0)
    if expr in consts:
        expr = consts[expr]
    cleaned = re.sub(r"(\d+)[uUlL]+\b", r"\1", expr)
    if not re.fullmatch(r"[0-9a-fA-FxX<>|&+()~\s]*", cleaned):
        raise ValueError("unsupported mask expression: %r" % expr)
    return eval(cleaned)  # 仅允许整数字面量与位运算符


def mask_to_bases(v):
    return [b for b in range(32) if (v >> b) & 1]


def parse_call(item):
    m = re.match(r"([A-Za-z_]\w*)\s*\(", item)
    if not m:
        raise ValueError("not a call: %r" % item[:60])
    name = m.group(1)
    depth = 0
    for j in range(m.end() - 1, len(item)):
        c = item[j]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return name, split_top(item[m.end():j])
    raise ValueError("unbalanced parens: %r" % item[:60])


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC_DEFAULT
    out = sys.argv[2] if len(sys.argv) > 2 else OUT_DEFAULT
    with open(src, "r", encoding="utf-8") as f:
        text = f.read()

    # constexpr 掩码名 → 表达式（如 kAxisAlignedSquareMask）。
    consts = {}
    for m in re.finditer(r"constexpr\s+(?:std::)?uint32_t\s+(\w+)\s*=\s*([^;]+);", text):
        consts[m.group(1)] = m.group(2)

    # 提取 initDefaultCity 函数体。
    mi = re.search(r"void\s+initDefaultCity\s*\(", text)
    body, _ = extract_braced(text, text.index("{", mi.end()))
    body = re.sub(r"//[^\n]*", "", body)  # 去行注释（表内无字符串字面量，安全）

    sets = {}      # key: 枚举名或 square/hex/tri
    order = []     # 保持 cpp 中出现顺序
    varmap = {}    # 局部引用变量名 → 密铺键

    decl_re = re.compile(
        r"auto&\s+(\w+)\s*=\s*c\.sets\[static_cast<size_t>\(static_cast<int>\("
        r"TilingType::(\w+)\)\)\]")
    assign_re = re.compile(r"\b(\w+)\.(levels|iconLevels|shapes|shapeLevelIndex)\s*=\s*")

    pos = 0
    while True:
        dm = decl_re.search(body, pos)
        am = assign_re.search(body, pos)
        if not am:
            break
        if dm and dm.start() < am.start():
            varmap[dm.group(1)] = dm.group(2)
            pos = dm.end()
            continue
        var, field = am.group(1), am.group(2)
        if var not in varmap and var not in ("square", "hex", "tri"):
            pos = am.end()  # 兜底循环里的局部 set.shapes = {single()} 等，跳过
            continue
        eq = body.index("{", am.end())
        inner, end = extract_braced(body, eq)
        key = varmap.get(var, var)
        if key not in sets:
            sets[key] = {}
            order.append(key)
        if field == "shapes":
            shapes = []
            for item in split_top(inner):
                name, args = parse_call(item)
                if name == "sq":
                    w, h = int(args[0]), int(args[1])
                    shapes.append({"anchorN": 0, "mask": None, "cells": expand_sq(w, h)})
                elif name == "hx":
                    shapes.append({"anchorN": 0, "mask": None,
                                   "cells": [[c[0], c[1]] for c in parse_cells(args[0])]})
                elif name == "tr":
                    shapes.append({"anchorN": 0, "mask": None, "cells": parse_cells(args[0])})
                elif name == "shp":
                    mask = eval_mask(args[2], consts) if len(args) > 2 else None
                    shapes.append({"mask": mask, "cells": parse_cells(args[1])})
                else:
                    raise ValueError("unknown shape ctor %r" % name)
            sets[key]["shapes"] = shapes
        else:
            vals = (parse_int_list(inner) if field == "shapeLevelIndex"
                    else parse_int_list(inner) if field == "iconLevels"
                    else parse_scalar_list(inner))
            sets[key][field] = vals
        pos = end

    n_shapes_total = 0
    # 键名用 tilingName 约定（Arch488 → arch_488）。
    def camel_to_key(name):
        # square/hex/tri 已是 tilingName；Arch488 → arch_488、Laves33336 → laves_33336。
        if name in ("square", "hex", "tri"):
            return name
        m = re.fullmatch(r"([A-Za-z]+)(\d+)", name)
        assert m, name
        return "%s_%s" % (m.group(1).lower(), m.group(2))

    # 读 tiling_specs：按边数分组建议（_byEdgeCount，程序不读取，供手动填 baseGroups 参考）。
    specs = {}
    for spec_file in ("data/tiling_specs_arch.json", "data/tiling_specs_laves.json"):
        try:
            with open(spec_file, encoding="utf-8") as sf:
                specs.update(json.load(sf))
        except OSError:
            pass

    def by_edge_count(key):
        sp = specs.get(key)
        if not sp:
            return None
        groups = {}
        for i, c in enumerate(sp["cells"]):
            groups.setdefault("n%d" % c.get("n", 0), []).append(i)
        return groups

    result = {"_comment": COMMENT_LINES}
    for key in order:
        d = sets[key]
        levels, shapes = d["levels"], d["shapes"]
        sli = d.get("shapeLevelIndex") or list(range(len(shapes)))
        assert len(sli) == len(shapes), "%s: shapeLevelIndex 与 shapes 数不一致" % key
        entry = {"levels": levels}
        if d.get("iconLevels"):
            entry["iconLevels"] = d["iconLevels"]
        js = []
        for j, sh in enumerate(shapes):
            # 方案A（2026-08-26）：半正/Laves 源表按旧中间朝向存储（= 世界帧 rot90⁻¹），
            # 此处烘焙回世界帧：(dx,dy) → (-dy,dx)，使 JSON 与屏幕朝向一致、运行时无需旋转。
            # square/hex/tri 在 Config.cpp 中已直接存世界偏移，不再旋转。
            if key not in ("square", "hex", "tri"):
                sh["cells"] = [[-c[1], c[0]] for c in sh["cells"]]
            rec = {"level": levels[sli[j]]}
            if sh["mask"]:
                rec["anchorBases"] = mask_to_bases(sh["mask"])
            rec["cells"] = sh["cells"]
            js.append(rec)
            n_shapes_total += 1
        entry["shapes"] = js
        sug = by_edge_count(camel_to_key(key))
        if sug:
            entry["_byEdgeCount"] = sug
        result[camel_to_key(key)] = entry

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("{\n")
        f.write('  "_comment": [\n')
        for i, line in enumerate(COMMENT_LINES):
            f.write('    %s%s\n' % (json.dumps(line, ensure_ascii=False),
                                    "," if i < len(COMMENT_LINES) - 1 else ""))
        f.write("  ],\n")
        keys = [k for k in result if k != "_comment"]
        for ki, key in enumerate(keys):
            e = result[key]
            f.write('  %s: {\n' % json.dumps(key))
            f.write('    "levels": %s,\n' % json.dumps(e["levels"]))
            if "iconLevels" in e:
                f.write('    "iconLevels": %s,\n' % json.dumps(e["iconLevels"]))
            f.write('    "baseGroups": {},')
            sug = e.get("_byEdgeCount")
            if sug:
                f.write('    "_byEdgeCount": %s,' % json.dumps(sug))
            f.write('\n')
            f.write('    "shapes": [\n')
            for si, sh in enumerate(e["shapes"]):
                head = ['"level": %s' % json.dumps(sh["level"])]
                if "anchorN" in sh:
                    head.append('"anchorN": %d' % sh["anchorN"])
                if "anchorBases" in sh:
                    head.append('"anchorBases": %s' % json.dumps(sh["anchorBases"]))
                f.write('      { %s, "cells": [\n' % ", ".join(head))
                for ci, cell in enumerate(sh["cells"]):
                    f.write('        [%s]%s\n' %
                            (", ".join(json.dumps(v) for v in cell),
                             "," if ci < len(sh["cells"]) - 1 else ""))
                f.write('      ]}%s\n' % ("," if si < len(e["shapes"]) - 1 else ""))
            f.write('    ]\n')
            f.write('  }%s\n' % ("," if ki < len(keys) - 1 else ""))
        f.write("}\n")

    print("sets: %d, shapes: %d -> %s" % (len(keys), n_shapes_total, out))
    for key in keys:
        print("  %-12s levels=%d shapes=%d" %
              (key, len(result[key]["levels"]), len(result[key]["shapes"])))


if __name__ == "__main__":
    main()
