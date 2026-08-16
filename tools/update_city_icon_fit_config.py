#!/usr/bin/env python3
"""把 iconFitScale / iconFitOffsetY JSON 片段合入 data/config.json 的 render.city。

用法（独立使用）：
    python3 tools/update_city_icon_fit_config.py <iconFitScale.json> <data/config.json>

通常由 tools/apply_city_icon_fit_scales.py 调用 update_config()。
只做最小文本插入（保留原 config.json 的缩进/注释/CRLF），并替换已存在的
render.city.iconFitScale / render.city.iconFitOffsetY 块。
"""
import json
import sys
from pathlib import Path

I6 = "      "
I8 = "        "
I10 = "          "
I12 = "            "


def build_scale_block(data: dict) -> list[str]:
    """生成 render.city.iconFitScale 文本块（首行到闭括号；闭括号不含逗号）。"""
    lines = [I6 + '"iconFitScale": {']
    tilings = list(data.keys())
    for ti, tname in enumerate(tilings):
        levels = data[tname]
        if not isinstance(levels, dict) or not levels:
            continue
        lines.append(I8 + json.dumps(tname) + ": {")
        lv_keys = sorted(levels.keys(), key=lambda s: int(s))
        for li, lv in enumerate(lv_keys):
            comma = "," if li + 1 < len(lv_keys) else ""
            lines.append(I10 + json.dumps(lv) + ": " + str(levels[lv]) + comma)
        comma = "," if ti + 1 < len(tilings) else ""
        lines.append(I8 + "}" + comma)
    lines.append(I6 + "}")
    return lines


def build_offset_block(data: dict) -> list[str]:
    """生成 render.city.iconFitOffsetY 文本块（首行到闭括号；闭括号不含逗号）。"""
    lines = [I6 + '"iconFitOffsetY": {']
    tilings = list(data.keys())
    for ti, tname in enumerate(tilings):
        records = data[tname]
        if not isinstance(records, list) or not records:
            continue
        lines.append(I8 + json.dumps(tname) + ": [")
        for ri, rec in enumerate(records):
            lines.append(I10 + "{")
            lines.append(I12 + '"iconLevel": ' + str(int(rec["iconLevel"])) + ",")
            lines.append(I12 + '"variant": ' + str(int(rec["variant"])) + ",")
            lines.append(I12 + '"anchorB": ' + str(int(rec["anchorB"])) + ",")
            lines.append(I12 + '"value": ' + str(float(rec["value"])))
            comma = "," if ri + 1 < len(records) else ""
            lines.append(I10 + "}" + comma)
        comma = "," if ti + 1 < len(tilings) else ""
        lines.append(I8 + "]" + comma)
    lines.append(I6 + "}")
    return lines


def _strip_old_blocks(lines: list[str]) -> list[str]:
    out = []
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith('"iconFitScale": {') or stripped.startswith('"iconFitOffsetY": {'):
            while i < len(lines):
                if lines[i].rstrip() == I6 + "}" or lines[i].rstrip() == I6 + "},":
                    i += 1
                    break
                i += 1
            continue
        out.append(lines[i])
        i += 1
    return out


def update_config(cfg_path: Path, scale_data: dict | None = None,
                  offset_data: dict | None = None) -> None:
    """把 scale_data / offset_data 合入 config.json（最小文本插入，保留 CRLF/注释）。"""
    scale_data = scale_data or {}
    offset_data = offset_data or {}
    if not scale_data and not offset_data:
        return

    text = cfg_path.read_bytes().decode("utf-8")
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()

    lines = _strip_old_blocks(lines)

    insert_idx = None
    for idx, line in enumerate(lines):
        if line.strip().startswith('"iconScale"'):
            insert_idx = idx + 1
            break
    if insert_idx is None:
        raise RuntimeError("找不到 render.city.iconScale 行，无法定位插入点")

    # iconScale 行尾必须有逗号（后面至少会插入一个块）。
    if not lines[insert_idx - 1].rstrip().endswith(","):
        lines[insert_idx - 1] = lines[insert_idx - 1].rstrip() + ","

    block: list[str] = []
    if scale_data:
        sl = build_scale_block(scale_data)
        if offset_data:
            sl[-1] = sl[-1] + ","
        block.extend(sl)
    if offset_data:
        block.extend(build_offset_block(offset_data))

    lines = lines[:insert_idx] + block + lines[insert_idx:]
    cfg_path.write_bytes(newline.join(lines).encode("utf-8"))


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    frag_path = Path(sys.argv[1])
    cfg_path = Path(sys.argv[2])
    data = json.loads(frag_path.read_text(encoding="utf-8"))
    update_config(cfg_path, scale_data=data)
    print(f"已更新 {cfg_path}: {len(data)} 种密铺")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
