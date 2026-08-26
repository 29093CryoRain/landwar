# -*- coding: utf-8 -*-
"""preview_city_shape.py — 城市形状点图预览（交互式）。

实时读取 data/city_shapes.json：输入密铺名 + 序号 n（shapes 数组中从上往下第 n 个，
1 起），把该形状的所有 cells 坐标画成点。每次查询都重新读文件，改完 JSON 直接回车重查。

用法：
    python tools/preview_city_shape.py
命令：
    密铺名   如 arch_31212（q / quit 退出）
    n        从上往下第几种城；直接回车 = 1
"""
import json
import os
import sys

import matplotlib.pyplot as plt

try:
    _DIR = os.path.dirname(os.path.abspath(__file__))
except NameError:  # exec 环境无 __file__
    _DIR = os.path.abspath(os.path.join(os.getcwd(), "tools"))
PATH = os.path.join(_DIR, "..", "data", "city_shapes.json")


def load():
    with open(PATH, encoding="utf-8") as f:
        return json.load(f)


def ask(prompt):
    try:
        return input(prompt).strip()
    except (EOFError, KeyboardInterrupt):
        return "q"


def show(d, name, n):
    entry = d.get(name)
    if not isinstance(entry, dict):
        print("未找到密铺 '%s'（可用：%s）" % (name, ", ".join(k for k in d if k != "_comment")))
        return
    shapes = entry["shapes"]
    if not (1 <= n <= len(shapes)):
        print("序号超出范围 1..%d" % len(shapes))
        return
    sh = shapes[n - 1]
    disabled = bool(sh.get("disabled", False))
    pts = sh["cells"]
    xs = [c[0] for c in pts]
    ys = [c[1] for c in pts]

    plt.clf()
    tag = " [disabled]" if disabled else ""
    plt.title("%s 第 %d/%d 种城%s  level=%s  anchorBases=%s"
              % (name, n, len(shapes), tag, sh.get("level"), sh.get("anchorBases", "(不限)")))
    plt.scatter(xs, ys, s=90, zorder=3,
                facecolors="none" if disabled else "tab:blue",
                edgecolors="gray" if disabled else "tab:blue")
    plt.scatter([0], [0], marker="x", color="red", s=120, zorder=4, label="锚格(0,0)")
    for x, y in zip(xs, ys):
        plt.annotate("(%.2f, %.2f)" % (x, y), (x, y), textcoords="offset points",
                     xytext=(6, 6), fontsize=7)
    plt.gca().set_aspect("equal")
    plt.grid(True, alpha=0.4)
    plt.axhline(0, color="k", lw=0.5)
    plt.axvline(0, color="k", lw=0.5)
    plt.legend(loc="best")
    print("%d 格%s" % (len(pts), "（已禁用，空心显示）" if disabled else ""))


def main():
    plt.ion()
    fig = plt.figure("city_shapes preview")
    print(__doc__)
    cur = None
    while True:
        line = ask("密铺名 [当前:%s] (q 退出): " % (cur or "-"))
        if line.lower() in ("q", "quit", "exit"):
            break
        if line:
            cur = line
        if cur is None:
            continue
        n_line = ask("第几种城(回车=1): ")
        try:
            n = int(n_line) if n_line else 1
        except ValueError:
            print("请输入数字")
            continue
        show(load(), cur, n)
        fig.canvas.draw_idle()
        plt.pause(0.01)


if __name__ == "__main__":
    sys.exit(main())
