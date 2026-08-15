# -*- coding: utf-8 -*-
"""
导出器：读取 recipes/*.json -> 生成 recipes_gen.h（FilmRecipe C 结构体数组）
供 ARM 端 filmsim.c 编译使用（配方与 PC 端 engine.py 完全一致）。
用法: python export_recipes.py [--out ../arm/recipes_gen.h]
"""
import json
import os
import sys
import argparse

HERE = os.path.dirname(os.path.abspath(__file__))
RECIPES_DIR = os.path.join(HERE, "recipes")
DEFAULT_OUT = os.path.join(HERE, "arm", "recipes_gen.h")

FIELDS = ["id", "name", "matrix", "saturation", "curve", "grain", "bw"]


def fmt_recipe(r):
    m = r["matrix"]
    matrix = "{{%g,%g,%g},{%g,%g,%g},{%g,%g,%g}}" % (
        m[0][0], m[0][1], m[0][2],
        m[1][0], m[1][1], m[1][2],
        m[2][0], m[2][1], m[2][2])
    c = r.get("curve", {})
    g = r.get("grain", {})
    return (
        '  {"%s", "%s", %s, %g, %g, %g, %g, %g, %g, %d, %g},' % (
            r["id"], r["name"], matrix,
            r.get("saturation", 1.0),
            c.get("contrast", 1.0),
            c.get("shadow_lift", 0.0),
            c.get("highlight_rolloff", 0.0),
            g.get("intensity", 0.3),
            g.get("size", 1.0),
            1 if g.get("mono", False) else 0,
            r.get("strength", 1.0)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    items = []
    for name in sorted(os.listdir(RECIPES_DIR)):
        if name.endswith(".json"):
            with open(os.path.join(RECIPES_DIR, name), "r", encoding="utf-8") as f:
                items.append(json.load(f))

    lines = [
        "/* 由 export_recipes.py 自动生成，勿手改 */",
        "#ifndef RECIPES_GEN_H_",
        "#define RECIPES_GEN_H_",
        "",
        "typedef struct {",
        "    const char *id;",
        "    const char *name;",
        "    double matrix[3][3];",
        "    double saturation;",
        "    double contrast;",
        "    double shadow_lift;",
        "    double highlight_rolloff;",
        "    double grain_intensity;",
        "    double grain_size;",
        "    int grain_mono;",
        "    double strength;",
        "} FilmRecipe;",
        "",
        "static const FilmRecipe RECIPES[] = {",
    ]
    for it in items:
        lines.append(fmt_recipe(it))
    lines += [
        "};",
        "",
        "#define NUM_RECIPES (int)(sizeof(RECIPES)/sizeof(RECIPES[0]))",
        "",
        "#endif /* RECIPES_GEN_H_ */",
        "",
    ]
    out = args.out
    content = "\n".join(lines)
    if out == "-":
        sys.stdout.write(content)
    else:
        with open(out, "w", encoding="utf-8") as f:
            f.write(content)
    print(f"已生成 {out}（{len(items)} 个配方）")


if __name__ == "__main__":
    main()
