# -*- coding: utf-8 -*-
"""把配方烘焙成 .cube 3D LUT（验证 .cube 链路 + 提供示例 LUT）。
用法: python make_cube.py <recipe_id> [size] [--out xx.cube]"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from engine import load_recipe, build_curve_lut

def clamp01(v):
    return 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)

def bake(rid, size=33, out=None):
    rec = load_recipe(rid)
    m = rec["matrix"]
    sat = rec.get("saturation", 1.0)
    c = rec.get("curve", {})
    lut = build_curve_lut(c.get("contrast", 1.0),
                          c.get("shadow_lift", 0.0),
                          c.get("highlight_rolloff", 0.0))
    rows = []
    for i in range(size):
        r_in = i / (size - 1.0)
        for j in range(size):
            g_in = j / (size - 1.0)
            for k in range(size):
                b_in = k / (size - 1.0)
                # 矩阵
                r = m[0][0]*r_in + m[0][1]*g_in + m[0][2]*b_in
                g = m[1][0]*r_in + m[1][1]*g_in + m[1][2]*b_in
                b = m[2][0]*r_in + m[2][1]*g_in + m[2][2]*b_in
                # 饱和度
                lum = 0.299*r + 0.587*g + 0.114*b
                r = lum + (r - lum)*sat
                g = lum + (g - lum)*sat
                b = lum + (b - lum)*sat
                # 曲线
                r = lut[int(clamp01(r)*255.0 + 0.5)] / 255.0
                g = lut[int(clamp01(g)*255.0 + 0.5)] / 255.0
                b = lut[int(clamp01(b)*255.0 + 0.5)] / 255.0
                rows.append((r, g, b))
    if out is None:
        out = os.path.join(HERE, "luts", rid + ".cube")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(f'TITLE "{rid} baked"\n')
        f.write(f"LUT_3D_SIZE {size}\n")
        f.write("DOMAIN_MIN 0.0 0.0 0.0\n")
        f.write("DOMAIN_MAX 1.0 1.0 1.0\n")
        for r, g, b in rows:
            f.write(f"{r:.6f} {g:.6f} {b:.6f}\n")
    print(f"已生成 {out}（{size}^3 = {len(rows)} 行）")

if __name__ == "__main__":
    rid = sys.argv[1] if len(sys.argv) > 1 else "velvia50"
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 33
    bake(rid, size)
