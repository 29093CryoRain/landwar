#!/usr/bin/env python3
"""读取交互工具临时数据 data/city_icon_fit_scales.json，写回外置表 data/city_icon_fits.json。

city_icon_fits.json = { "iconFitScale": {...}, "iconFitOffsetY": {...} }，取代原先写回
config.json render.city 的两块（2026-08-26 外置）。

处理规则：
  1. scale：同一密铺 + 同一贴图等级（iconLevel）下多个已保存条目 → 取最小值作为该密铺
     该贴图等级的 iconFitScale。
  2. offsetY：按 "每个形状的每类锚基础格" 去重——类别 = baseGroups 的一个数组
     （city_shapes.json）。同类（同 tiling + iconLevel + variant + anchorClass）内有不同
     数值时取均值；每类一条，携带该类全部基础格 bases；写入 iconFitOffsetY。
  3. 写 data/city_icon_fits.json（忽略 config.json；config.json 已不含这两块）。

用法：
    python3 tools/apply_city_icon_fit_scales.py
"""
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _load(name, default=None):
    p = ROOT / "data" / name
    if not p.exists():
        return default
    return json.loads(p.read_text(encoding="utf-8"))


def base_classes(tname, shapes_json):
    """该密铺的 baseGroups：返回 (groupName -> [bases], base -> groupName)。"""
    e = shapes_json.get(tname, {})
    bg = {
        k: v
        for k, v in e.get("baseGroups", {}).items()
        if not k.startswith("_") and isinstance(v, list) and v
    }
    base2group = {}
    for g, bases in bg.items():
        for b in bases:
            base2group[b] = g
    return bg, base2group


def main():
    src = ROOT / "data" / "city_icon_fit_scales.json"
    cfg_path = ROOT / "data" / "config.json"
    if not src.exists():
        print(f"找不到临时数据文件: {src}", file=sys.stderr)
        return 1
    shapes = _load("city_shapes.json", {})
    records = json.loads(src.read_text(encoding="utf-8")).get("records", [])
    if not records:
        print("临时数据文件里没有 records", file=sys.stderr)
        return 1

    fit: dict[str, dict[str, float]] = {}
    offsets: dict[str, list[dict]] = {}
    average_note: dict[str, list] = {}  # keyish -> [values]（仅打印差值用）

    for rec in records:
        tiling = rec.get("tiling")
        icon_level = rec.get("iconLevel")
        variant = rec.get("variantIndex")
        anchor_b = rec.get("anchorB")
        scale = rec.get("scale")
        offset_y = rec.get("offsetY", 0.0)
        if not isinstance(tiling, str) or not isinstance(icon_level, int):
            continue
        if not isinstance(scale, (int, float)):
            continue
        if not isinstance(variant, int):
            variant = 0
        if not isinstance(anchor_b, int):
            anchor_b = 0
        if not isinstance(offset_y, (int, float)):
            offset_y = 0.0

        # scale：同密铺同贴图等级取最小
        bucket = fit.setdefault(tiling, {})
        key = str(icon_level)
        if key not in bucket or scale < bucket[key]:
            bucket[key] = float(scale)

        # offsetY：按 (iconLevel, variant, anchorClass) 分组，同类取均值
        _, base2group = base_classes(tiling, shapes)
        cls = base2group.get(anchor_b, f"base_{anchor_b}")
        grp = offsets.setdefault(tiling, {})
        agg = grp.setdefault((icon_level, variant, cls), [])
        agg.append((anchor_b, float(offset_y)))

    # 生成 offsetY 输出（每类一条，bases = 该类全部基础格，value = 均值）
    out_off: dict[str, list[dict]] = {}
    for tiling, grp in offsets.items():
        _, base2group = base_classes(tiling, shapes)
        bg, _ = base_classes(tiling, shapes)
        for (icon_level, variant, cls), vals in grp.items():
            bases = sorted(bg.get(cls, [vals[0][0]]))
            avg = sum(v for _, v in vals) / len(vals)
            out_off.setdefault(tiling, []).append(
                {"iconLevel": icon_level, "variant": variant, "bases": bases, "value": avg}
            )
            if len({v for _, v in vals}) > 1:
                average_note.setdefault(tiling, []).append(
                    {"iconLevel": icon_level, "variant": variant, "class": cls,
                     "values": [v for _, v in vals], "avg": avg}
                )

    out = {
        "_comment": [
            "城市贴图预计算缩放/竖直平移表（外置于 config.json，2026-08-26）。",
            "由 tools/city_icon_fit_tool 生成临时 data/city_icon_fit_scales.json，",
            "经 tools/apply_city_icon_fit_scales.py 去重写回本文件。",
            "iconFitScale 键 = tiling -> 贴图等级 -> fitW(世界单位)；iconFitOffsetY 每项 =",
            "{iconLevel, variant(同级形状变体下标), bases(该锚基础格类的全部格), value(世界单位竖向平移，同类均值)}。",
        ],
        "iconFitScale": fit,
        "iconFitOffsetY": out_off,
    }
    out_path = ROOT / "data" / "city_icon_fits.json"
    out_path.write_text(json.dumps(out, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"已写回 {out_path}")
    print(f"  iconFitScale: {len(fit)} 种密铺")
    print(f"  iconFitOffsetY: {len(out_off)} 种密铺 / {sum(len(v) for v in out_off.values())} 条")
    if average_note:
        print("  [同类内有不同数值 → 已取平均]")
        for tiling in sorted(average_note):
            for n in average_note[tiling]:
                print(f"    {tiling} iconLevel={n['iconLevel']} variant={n['variant']} "
                      f"class={n['class']}: {n['values']} -> {n['avg']:.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
