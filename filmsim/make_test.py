# -*- coding: utf-8 -*-
"""生成模拟相机测试照片（天空渐变/肤色块/绿色植被/中性灰），供胶片仿真引擎测试。"""
import argparse
import os
from PIL import Image, ImageDraw, ImageFilter


def make_scene(w=900, h=600, variant=0):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = x / w
            if y < h * 0.55:                       # 天空
                r, g, b = int(120 + 60 * t), int(150 + 40 * t), int(200 - 30 * t)
            else:                                  # 地面
                r, g, b = int(90 + 60 * t), int(130 + 40 * t), int(70 + 20 * t)
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(img)
    d.ellipse((w * 0.72, h * 0.12, w * 0.86, h * 0.30), fill=(250, 240, 210))          # 太阳
    d.rectangle((w * 0.08, h * 0.30, w * 0.28, h * 0.50), fill=(210, 160, 120))        # 肤色块
    d.rectangle((w * 0.35, h * 0.60, w * 0.65, h * 0.95), fill=(60, 130, 60))          # 植被
    d.rectangle((w * 0.78, h * 0.60, w * 0.95, h * 0.95), fill=(128, 128, 128))        # 中性灰
    if variant == 1:                                                                    # 冷调场景
        d.rectangle((w * 0.30, h * 0.10, w * 0.55, h * 0.35), fill=(140, 170, 220))
    if variant == 2:                                                                    # 暖调场景
        d.rectangle((w * 0.30, h * 0.10, w * 0.55, h * 0.35), fill=(220, 160, 90))
    return img.filter(ImageFilter.GaussianBlur(1))


def main():
    ap = argparse.ArgumentParser(description="生成模拟相机测试照片")
    ap.add_argument("out_dir", help="输出目录（DCIM 模拟）")
    ap.add_argument("--count", type=int, default=3, help="生成张数")
    ap.add_argument("--prefix", default="100PHOTO_IMG_", help="文件名前缀")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    for i in range(args.count):
        im = make_scene(variant=i % 3)
        name = f"{args.prefix}{i + 1:04d}.JPG"
        im.save(os.path.join(args.out_dir, name), quality=92)
        print(f"生成: {name}")


if __name__ == "__main__":
    main()
