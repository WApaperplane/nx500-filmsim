# -*- coding: utf-8 -*-
"""
胶片仿真引擎原型 (PC 验证版)
纯 Pillow 实现：色彩矩阵 -> 饱和度 -> 曲线 LUT -> 颗粒
阶段 2 (ARM 实机) 时，本模块算法将移植为 C 实现（可用 tint/libjpeg-turbo 或 ImageMagick Q16）。
配方格式见 recipes/*.json；lut3d (.cube) 字段预留，解析器阶段 2 实现。
"""
import json
import math
import os
from PIL import Image, ImageChops, ImageEnhance, ImageFilter

DEFAULT_QUALITY = 92

# ---------------------------------------------------------------- 核心处理

def load_recipe(recipe_id_or_path):
    """加载配方：内置 id（recipes/*.json）或直接文件路径。"""
    if os.path.isfile(recipe_id_or_path):
        path = recipe_id_or_path
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        path = os.path.join(here, "recipes", recipe_id_or_path + ".json")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def list_recipes():
    """列出 recipes/ 下所有内置配方。"""
    here = os.path.dirname(os.path.abspath(__file__))
    d = os.path.join(here, "recipes")
    out = []
    for name in sorted(os.listdir(d)):
        if name.endswith(".json"):
            try:
                r = load_recipe(os.path.join(d, name))
                out.append({"id": r["id"], "name": r["name"], "family": r["family"]})
            except Exception:
                pass
    return out


def apply_matrix(img, m):
    """3x3 色彩矩阵（跨通道线性组合），ImageChops 实现。"""
    r, g, b = img.split()
    sz = img.size

    def lin(chan, w):
        if abs(w) < 1e-6:
            return Image.new("L", sz, 0)
        return chan.point(lambda v: int(round(v * w)))

    def comb(wrow):
        c = Image.new("L", sz, 0)
        c = ImageChops.add(c, lin(r, wrow[0]))
        c = ImageChops.add(c, lin(g, wrow[1]))
        c = ImageChops.add(c, lin(b, wrow[2]))
        return c

    R = comb(m[0])
    G = comb(m[1])
    B = comb(m[2])
    return Image.merge("RGB", (R, G, B))


def build_curve_lut(contrast=1.0, shadow_lift=0.0, highlight_rolloff=0.0):
    """构造 256 长度的一维曲线 LUT：对比度 + 柔化 S 曲线 + 阴影提升 + 高光滚降。"""
    lut = []
    k = max(0.1, 6.0 * contrast)
    for i in range(256):
        x = i / 255.0
        x = (x - 0.5) * contrast + 0.5          # 线性对比
        x = max(0.0, min(1.0, x))
        x = 1.0 / (1.0 + math.exp(-k * (x - 0.5)))  # S 曲线
        x = x * (1.0 - highlight_rolloff) + shadow_lift
        x = max(0.0, min(1.0, x))
        lut.append(int(round(x * 255)))
    return lut


def apply_grain(img, grain):
    """数学胶片颗粒：高斯噪声 + 模糊(粒度) + overlay 混合。mono=True 时只加亮度颗粒。"""
    intensity = float(grain.get("intensity", 0.3))
    size = float(grain.get("size", 1.0))
    mono = bool(grain.get("mono", False))
    if intensity <= 0:
        return img
    sigma = 6.0 + 45.0 * intensity              # 噪声幅度
    noise = Image.effect_noise(img.size, sigma)
    if size > 1.0:
        noise = noise.filter(ImageFilter.GaussianBlur(radius=(size - 1.0) * 1.4))
    if mono:
        n = noise.convert("L")
        n_rgb = Image.merge("RGB", (n, n, n))
    else:
        n_rgb = noise.convert("RGB")
    return ImageChops.overlay(img, n_rgb)


def apply_curve(img, lut):
    """一维曲线 LUT：RGB 逐通道应用（Pillow 12 的 point 对 RGB 要求 1024 项）。"""
    if img.mode == "RGB":
        r, g, b = img.split()
        return Image.merge("RGB", (r.point(lut), g.point(lut), b.point(lut)))
    return img.point(lut)


def load_cube(path):
    """解析 .cube 3D LUT 文件（Adobe/Resolve 通用格式）。
    返回 {'size': n, 'data': [[r,g,b],...] 共 n^3 行}，值域 0..1。"""
    size = None
    data = []
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            if s.startswith("LUT_3D_SIZE"):
                size = int(s.split()[-1])
                continue
            if s.startswith(("TITLE", "DOMAIN", "LUT_1D", "LUT_3D_INPUT")):
                continue
            parts = s.split()
            if len(parts) >= 3:
                try:
                    data.append([float(parts[0]), float(parts[1]), float(parts[2])])
                except ValueError:
                    pass
    if size is None:
        size = int(round(len(data) ** (1.0 / 3.0)))
    return {"size": size, "data": data}


def _cube_lookup(cube, rf, gf, bf):
    """3D LUT 三线性插值：输入/输出均为 0..1 浮点 RGB。"""
    size = cube["size"]
    data = cube["data"]
    r = rf * (size - 1.0)
    g = gf * (size - 1.0)
    b = bf * (size - 1.0)
    ri = int(r); gi = int(g); bi = int(b)
    if ri > size - 2: ri = size - 2
    if gi > size - 2: gi = size - 2
    if bi > size - 2: bi = size - 2
    dr = r - ri; dg = g - gi; db = b - bi

    def at(i, j, k):
        return data[(i * size + j) * size + k]

    c000 = at(ri, gi, bi);     c100 = at(ri + 1, gi, bi)
    c010 = at(ri, gi + 1, bi); c110 = at(ri + 1, gi + 1, bi)
    c001 = at(ri, gi, bi + 1); c101 = at(ri + 1, gi, bi + 1)
    c011 = at(ri, gi + 1, bi + 1); c111 = at(ri + 1, gi + 1, bi + 1)
    out = [0.0, 0.0, 0.0]
    for ch in range(3):
        v = (c000[ch] * (1 - dr) + c100[ch] * dr) * (1 - dg) * (1 - db)
        v += (c010[ch] * (1 - dr) + c110[ch] * dr) * dg * (1 - db)
        v += (c001[ch] * (1 - dr) + c101[ch] * dr) * (1 - dg) * db
        v += (c011[ch] * (1 - dr) + c111[ch] * dr) * dg * db
        out[ch] = v
    return out


def apply_cube(img, cube, strength=1.0):
    """对 PIL 图像应用 3D LUT（+strength 混合）。纯 Python 逐像素，大图较慢。"""
    img = img.convert("RGB")
    orig = img.copy()
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            c = _cube_lookup(cube, r / 255.0, g / 255.0, b / 255.0)
            nr = r + (c[0] * 255.0 - r) * strength
            ng = g + (c[1] * 255.0 - g) * strength
            nb = b + (c[2] * 255.0 - b) * strength
            px[x, y] = (int(max(0, min(255, nr))),
                        int(max(0, min(255, ng))),
                        int(max(0, min(255, nb))))
    return img


def apply_recipe(img, recipe):
    """对 PIL 图像应用一个配方。lut3d 为 .cube 文件路径时走 3D LUT 管线，否则走矩阵/曲线/颗粒管线。"""
    img = img.convert("RGB")
    orig = img.copy()
    if recipe.get("lut3d"):
        cube = load_cube(recipe["lut3d"])
        img = apply_cube(img, cube, 1.0)
    else:
        img = apply_matrix(img, recipe["matrix"])
        img = ImageEnhance.Color(img).enhance(recipe.get("saturation", 1.0))
        c = recipe.get("curve", {})
        img = apply_curve(img, build_curve_lut(c.get("contrast", 1.0),
                                               c.get("shadow_lift", 0.0),
                                               c.get("highlight_rolloff", 0.0)))
        img = apply_grain(img, recipe.get("grain", {}))
    # 整体强度：out = orig + (processed - orig) * strength
    strength = recipe.get("strength", 1.0)
    if abs(strength - 1.0) > 1e-6:
        img = Image.blend(orig, img, max(0.0, min(2.0, strength)))
    return img


def process_file(src_path, dst_path, recipe, quality=DEFAULT_QUALITY):
    """处理单个文件：src -> 应用配方 -> dst（调用方负责原子写回）。"""
    with Image.open(src_path) as im:
        out = apply_recipe(im, recipe)
        out.save(dst_path, quality=quality)
    return dst_path
