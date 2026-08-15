# NX500 / NX1 机内胶片仿真探索

In-camera film simulation research for the Samsung NX500 (and NX1).

本仓库记录在三星 NX500（固件 1.12，DRIMe5 平台）上实现"机内胶片仿真/滤镜"的完整探索：三条技术路线的过程、已验证结论、**NX500 capdtm 完整参数表**、工具链与踩坑记录，供未来开发者参考。

> 探索结论速览：
> - **ISP 级实时自定义滤镜（富士式）：不可达**（ISP/固件闭源，capdtm 无法写 Picture Wizard 子参数）
> - **程序化切换照片风格（12 种，ISP 实时）：可行**（`st cap capdtm setusr 20 <hex>`）
> - **CPU 拍后引擎：可用**（28MP 全分辨率 32s/张，矩阵/饱和度/曲线配方）
> - **3D LUT（.cube）在真机：花屏**（原因未明，已放弃）

## 文档

| 文件 | 说明 |
|---|---|
| [FILM_SIM_RESEARCH.zh.md](FILM_SIM_RESEARCH.zh.md) | 中文探索报告（完整） |
| [FILM_SIM_RESEARCH.en.md](FILM_SIM_RESEARCH.en.md) | English research report |
| [filmsim/deploy_nx500/DEPLOY_NX500.md](filmsim/deploy_nx500/DEPLOY_NX500.md) | NX500 部署指南 |

## 目录结构

```
FILM_SIM_RESEARCH.zh.md / .en.md   # 双语探索报告（含 capdtm 完整参数表 0-128）
filmsim/
  engine.py             # PC 原型引擎（矩阵/饱和度/曲线/颗粒/.cube/强度）
  make_test.py          # 模拟相机测试图生成
  make_cube.py          # 配方 → .cube 烘焙
  export_recipes.py     # 配方 JSON → C 头文件
  recipes/*.json        # 6 个内置配方（portra/velvia/trix/superia/hp5/ektachrome）
  arm/
    fs2.c               # ARM 引擎（NEON + 流式，配方路径可用）
    build.sh            # Zig 交叉编译脚本
    neon_test.c         # NEON 冒烟测试
    verify_elf.py       # ELF 属性验证
    recipes_gen.h       # 生成的配方 C 表
  deploy_nx500/
    capdtm_enum.sh      # capdtm 参数枚举（数字 ID）
    capdtm_pw.sh/pw2.sh # Picture Wizard 风格切换验证
    filmsim_test.sh     # 相机自动测试脚本
    DEPLOY_NX500.md     # 部署指南
```

## 快速开始（PC 端）

```bash
# 依赖：Python 3 + Pillow
python engine.py --help          # 查看用法（处理单张）
python make_test.py              # 生成模拟测试图
python make_cube.py velvia50     # 烘焙配方为 .cube
python export_recipes.py --out - # 配方 JSON → C 头文件
```

## 交叉编译（ARM）

```bash
# 依赖：Zig 0.13（自带 glibc 交叉编译）+ NX rootfs（标准-armv7l）
zig cc -target arm-linux-gnueabi.2.15 -O0 -mcpu=cortex_a9 \
       -mfloat-abi=soft -fno-stack-protector \
       -I<rootfs>/usr/include \
       filmsim/arm/fs2.c -L<rootfs>/usr/lib -ljpeg -o filmsim.arm
```

> ⚠️ **必须 `-O0`**：-O1/-O2 编译的 ARM 代码在 NX500 真机段错误（编译器 codegen 问题）。
> 完整踩坑记录见探索报告。

## capdtm 速查（NX500 固件 1.12）

```bash
# 枚举参数（数字 ID，不是名字）
for id in $(seq 0 128); do st cap capdtm getusr $id; done

# 切换照片风格（ISP 实时，拍 JPEG 生效）
st cap capdtm setusr 20 0x140002   # PW_PORTRAIT
st cap capdtm setusr 20 0x140005   # PW_RETRO
st cap capdtm setusr 20 0x140009   # PW_CUSTOM1
```

完整参数表（0-128，ID / 名称 / hex）见 [FILM_SIM_RESEARCH.zh.md](FILM_SIM_RESEARCH.zh.md#52-nx500-完整-capdtm-参数表枚举-0-128固件-112)。

## License

参考 [nx500_nx1_modding](https://github.com/WApaperplane/nx500_nx1_modding) 项目惯例。代码仅供研究学习。
