#!/usr/bin/env python3
"""读取交互工具临时数据 data/city_icon_fit_scales.json，写回 data/config.json。

处理规则：
  1. scale：同一密铺 + 同一贴图等级（iconLevel）下，可能有多个已保存条目
     （同级多个形状变体、同一形状不同锚朝向/正反三角）；取最小值作为该密铺
     该贴图等级的 render.city.iconFitScale。
  2. offsetY：每种城市的每种情况（tiling + iconLevel + variant + anchorB）独立保存，
     不做合并，写入 render.city.iconFitOffsetY。
  3. 用最小文本插入写回 config.json（保留 CRLF/注释，替换已有块）。

用法：
    python3 tools/apply_city_icon_fit_scales.py
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from update_city_icon_fit_config import update_config  # noqa: E402


def main() -> int:
    root = Path(__file__).resolve().parent
    src = root / "city_icon_fit_scales.json"
    if not src.exists():
        src = root.parent / "data" / "city_icon_fit_scales.json"
    if not src.exists():
        print(f"找不到临时数据文件: {src}", file=sys.stderr)
        return 1
    cfg = root.parent / "data" / "config.json"

    raw = json.loads(src.read_text(encoding="utf-8"))
    records = raw.get("records", [])
    if not records:
        print("临时数据文件里没有 records", file=sys.stderr)
        return 1

    fit: dict[str, dict[str, float]] = {}
    offsets: dict[str, list[dict]] = {}

    for rec in records:
        tiling = rec.get("tiling")
        icon_level = rec.get("iconLevel")
        variant = rec.get("variantIndex")
        anchor_b = rec.get("anchorB")
        scale = rec.get("scale")
        offset_y = rec.get("offsetY", 0.0)
        if not isinstance(tiling, str) or not isinstance(icon_level, int):
            print(f"跳过非法记录: {rec}", file=sys.stderr)
            continue
        if not isinstance(scale, (int, float)):
            print(f"跳过缺少 scale 的记录: {rec}", file=sys.stderr)
            continue
        if not isinstance(variant, int):
            variant = 0
        if not isinstance(anchor_b, int):
            anchor_b = 0
        if not isinstance(offset_y, (int, float)):
            offset_y = 0.0

        bucket = fit.setdefault(tiling, {})
        key = str(icon_level)
        if key not in bucket or scale < bucket[key]:
            bucket[key] = float(scale)

        offsets.setdefault(tiling, []).append({
            "iconLevel": icon_level,
            "variant": variant,
            "anchorB": anchor_b,
            "value": float(offset_y),
        })

    if not fit:
        print("没有可用记录", file=sys.stderr)
        return 1

    update_config(cfg, scale_data=fit, offset_data=offsets)
    print(f"已写回 {cfg}: {len(fit)} 种密铺的 scale + {sum(len(v) for v in offsets.values())} 条 offsetY")
    for tiling in sorted(fit):
        levels = sorted(fit[tiling].keys(), key=lambda s: int(s))
        print(f"  {tiling}: scale " + ", ".join(f"{lv}:{fit[tiling][lv]}" for lv in levels))
        if tiling in offsets:
            print(f"    offsetY: {offsets[tiling]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
