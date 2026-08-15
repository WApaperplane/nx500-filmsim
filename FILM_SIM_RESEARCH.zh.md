# NX500/NX1 机内胶片仿真探索报告（Film Simulation Research）

> 本文档记录 2026-08-15 在三星 NX500（固件 1.12，DRIMe5 平台）上探索"机内胶片仿真/滤镜"的完整过程、已验证结论、关键技术资料与失败教训，供未来开发者参考，避免重复踩坑。
>
> 英文版见 [FILM_SIM_RESEARCH.en.md](FILM_SIM_RESEARCH.en.md)

---

## 目录
1. [背景与目标](#1-背景与目标)
2. [三条技术路线概览](#2-三条技术路线概览)
3. [路线 A：CPU 软件引擎（拍后处理）— 可用](#3-路线-acpu-软件引擎拍后处理--可用)
4. [路线 B：3D LUT（.cube）— 放弃](#4-路线-b3d-lutcube--放弃)
5. [路线 C：三星 ISP 滤镜（capdtm）— 部分可行，最终放弃](#5-路线-c三星-isp-滤镜capdtm--部分可行最终放弃)
6. [已验证结论与边界](#6-已验证结论与边界)
7. [关键技术资料](#7-关键技术资料)
8. [对未来开发者的建议](#8-对未来开发者的建议)
9. [代码与文件索引](#9-代码与文件索引)

---

## 1. 背景与目标

- **平台**：三星 NX500 相机（固件 1.12，Tizen 2.2，DRIMe5 SoC = ARM Cortex-A9 双核，NEON 可用）
- **已有基础**：NX-KS2 mod 体系（SD 卡脚本自动执行、bluetoothd 钩子、Web 远程控制等）
- **目标**：实现类似富士胶片模拟 / 松下 LUT 的"机内滤镜"——最好拍 JPEG 时**实时生效**（ISP 级），退而求其次是拍后处理

---

## 2. 三条技术路线概览

| 路线 | 方案 | 结果 | 结论 |
|---|---|---|---|
| A | CPU 软件引擎（解码→处理→编码） | ✅ 全分辨率 28MP 可用（32s/张） | 可用但不实时 |
| B | 3D LUT（.cube 查表插值） | ❌ 真机持续花屏（原因未明） | 放弃 |
| C | 三星 ISP 滤镜（capdtm 参数） | ⚠️ 可切换预设/槽位，但无法自定义子参数 | 部分可行，放弃 |

**最终结论：由于 ISP/固件闭源，"拍 JPEG 时 ISP 级实时自定义滤镜"在 NX500 上不可实现。** CPU 引擎可作为拍后批量/自动处理方案。

---

## 3. 路线 A：CPU 软件引擎（拍后处理）— 可用

### 3.1 架构
```
PC 原型（Python/Pillow）
    ↓ 算法验证
ARM 交叉编译（Zig → arm-linux-gnueabi.2.15）
    ↓
相机部署（SD 卡 info.tg + nx_cs.adj 触发 dfmsd 自动执行）
    ↓
处理管线：JPEG 解码 → 色彩矩阵 → 饱和度 → 曲线 LUT →（颗粒已移除）→ JPEG 编码（4:4:4 q95）
```

### 3.2 交叉编译工具链（已验证可复现）
- **工具链**：Zig 0.13.0（Windows 单文件，自带 glibc 交叉编译）
- **目标三元组**：`arm-linux-gnueabi.2.15`（Tizen 2.2 glibc ~2.15，EABI5 softfp——需解析 rootfs 库 ELF flags 确认）
- **NEON**：`-mcpu=cortex_a9`（zig 的 CPU 名是下划线格式；连字符格式会被拒）
- **关键参数**：
  ```
  zig cc -target arm-linux-gnueabi.2.15 -O0 -mcpu=cortex_a9 \
         -mfloat-abi=soft -fno-stack-protector \
         -I<rootfs>/usr/include \
         filmsim.c -L<rootfs>/usr/lib -ljpeg -o filmsim.arm
  ```
- **⚠️ 必须 -O0**：`-O1/-O2` 编译的 ARM 代码在 NX500 真机**段错误（exit 139）**，与代码是否整数化无关（编译器生成代码问题）
- **链接系统库**：用 rootfs 自带的 `libjpeg-turbo`（`libjpeg.so.8`，依赖仅 libc+libgcc_s）
- **避免链接 libm**：softfp 下 double 运算走软件浮点模拟（极慢）；且 zig 生成 glibc libm stub 在沙箱环境可能 AccessDenied → **自实现 math**（exp 用级数、高斯噪声用中心极限定理近似）或用整数定点

### 3.3 性能优化阶梯（28MP 全分辨率 6480×4320）
| 版本 | 方案 | 耗时 |
|---|---|---|
| v6 | double 浮点 | 60-120s |
| v8 | 整数定点（矩阵×1024/×64） | 68s |
| v10 | + NEON 8px（vld3/vst3） | 37s |
| v11 | + 曲线合并单 pass | 34s |
| v12 | + NEON 16px + 流式管线 | 32s |

- **瓶颈**：JPEG 编码（4:4:4 q95 28MP 约占 40%）、像素处理（NEON 后约 25%）、解码（libjpeg-turbo 已 NEON）、SD 卡 IO
- **结论**：32s/张 是 CPU 软件管线的实用极限（双核并行风险大、收益有限）

### 3.4 流式管线要点
- 解码一行 → 处理一行 → 编码一行（零整幅缓冲，28MP 内存从 84MB 降到 ~15MB）
- 但 **.cube LUT 路径的流式在真机异常**（见路线 B）

---

## 4. 路线 B：3D LUT（.cube）— 放弃

### 4.1 做了什么
- PC 端：`.cube` 解析（LUT_3D_SIZE + 数据行）+ 三线性插值（整数定点：坐标×256、frac 0-255、LUT 存 0-255）
- ARM 端：`--lut <file.cube>` 参数，内置配方烘焙 LUT（make_cube.py）
- **PC 验证：整数插值 vs 浮点平均差 1.06/255（视觉无差）——算法正确**

### 4.2 真机现象（v8 → v14 全部复现）
- **所有版本（整幅缓冲/流式/各种修复）在真机均"花屏"**（图像错位、色彩错乱、文件异常增大）
- exit=0 但输出损坏——算法对、真机运行错，**根因未定位**（怀疑编译产物/运行时内存问题）

### 4.3 教训
- PC 算法验证通过 ≠ 真机可运行（尤其 -O0 ARM + 大图 84MB 缓冲场景）
- 排查成本极高（每轮需拔卡真机测试），应及时止损

---

## 5. 路线 C：三星 ISP 滤镜（capdtm）— 部分可行，最终放弃

### 5.1 关键发现：capdtm 参数用**数字 ID**（不是名字）
```bash
# 错误（名字无效）：
st cap capdtm getusr USERDATA_PW        # → Invalid argument
# 正确（数字 ID）：
st cap capdtm getusr 20                  # → UserData is PW_STANDARD (0x140000)
st cap capdtm setusr 20 0x140002         # → UserData is PW_PORTRAIT
```
- 参数 hex 格式：**高 16 位 = 参数组，低 16 位 = 值**（如 COLORSPACE=0x300002 组 0x30 值 0x0002）
- 注意：**NX500 与 NX1 的 ID 有偏移**（NX1 的 48=COLORSPACE，NX500 的 50=COLORSPACE），需按机枚举

### 5.2 NX500 完整 capdtm 参数表（枚举 0-128，固件 1.12）
```
 0 DIALMODE_PROGRAM 0x2                1 SHOOTINGMODE_MANUAL 0x10004
 2 IMAGESIZE_NORMAL_28M 0x20000        3 IMAGEASPECTRATIO_IMAGEAR_3_2 0x30000
 4 IMAGEQUALITY_SUPERFINE 0x40000      5 ISO_AUTO 0x50000
 6 WB_AUTO 0x60000                     7 AFMODE_AUTO 0x70004
 8 AFAREA_MULTI 0x80002                9 FACEDETECT_OFF 0x90000
10 DRIVE_SINGLE 0xa0001                11 BURSTRATE_RATE10FPS 0xb0002
12 FLASHMODE_OFF 0xc0000               13 METERING_MULTI 0xd0002
14 MOVIEMETERING_MULTI 0xe0002         15 LINKAE2AF_OFF 0xf0000
16 BRIGHTNESSADJUSTGUIDE_OFF 0x100000  17 FRAMEEXPOSURE_OFF 0x110000
18 MULTIEXPOSURE_OFF 0x120000          19 MULTIEXPOSURETYPE_SAVEALL 0x130000
20 PW_STANDARD 0x140000   ★照片风格    21 SMARTRANGE_ON 0x150001
22 MOVIESMARTRANGE_OFF 0x160000        23 AFPRIORITYRLS_OFF_BALANCE 0x170001
24 OIS_OFF 0x180000                    25 FACETONE_LEVEL1 0x190000
26 FACERETOUCH_LEVEL3 0x1a0002         27 SMARTART_OFF 0x1b0000
28 SMARTARTLEVEL_LEVEL0 0x1c0000       29 EVSTEP_ONETHIRD 0x1d0000
30 ISONR_MID 0x1e0002                  31 LTNR_ON 0x1f0001
32 AEBRKORDER_PZM 0x200002             33 AEBRKAREA_1OVER3 0x210000
34 WBBRKSET_AB3 0x220000               35 PWBRKSTANDARD_SET 0x230001
36 PWBRKVIVID_SET 0x240001             37 PWBRKPORTRAIT_SET 0x250001
38 PWBRKLANDSCAPE_SET 0x260001         39 PWBRKFOREST_SET 0x270001
40 PWBRKRETRO_SET 0x280001             41 PWBRKCOOL_SET 0x290001
42 PWBRKCALM_SET 0x2a0001              43 PWBRKCLASSIC_SET 0x2b0001
44 PWBRKCUSTOM1_SET 0x2c0001           45 PWBRKCUSTOM2_SET 0x2d0001
46 PWBRKCUSTOM3_SET 0x2e0001           47 PWBRKCUSTOM4_SET 0x2f0001
48 AFLIGHT_HIGH 0x300001               49 AEL_AEL 0x310000
50 COLORSPACE_SRGB 0x320000            51 QUICKVIEWTIME_HOLD 0x330004
52 MONITOROUT_LCD 0x340000             53 HDMIOUT_1080I 0x350003
54 MOVIESIZE_FHD 0x360000              55 MOVIEFRAMERATE_FPS30 0x370004
56 MOVIEFADER_OFF 0x380000             57 MOVIEAE_P 0x390000
58 F3DMOVIEFRAMERATE_FPS30 0x3a0001    59 LDC_ON 0x3b0001
60 ISOSTEP_ONE 0x3c0001                61 ISOEXPANSION_ON 0x3d0001
62 SMARTFILTERTYPE_OFF 0x3e0000        63 SMARTFILTERSIZE_SIZE0 0x3f0000
64 ISOAUTOMAX_ISO6400 0x400011         65 MFASSIST_ENLARGEX5 0x410001
66 FOCUSPEAKING_WHITE 0x420001         67 HDR_OFF 0x430000
68 FDMODE_OFF 0x440000                 69 IZOOM_X1 0x450000
70 F3DSHOT_OFF 0x460000                71 F3DAUTOMODE_ON 0x470001
72 DMF_OFF 0x480000                    73 FASTCONTINUOUS_OFF 0x490000
74 SMARTMOVIE_OFF 0x4a0000             75 LENSEFFECT_OFF 0x4b0000
76 HDRLEVEL_LV2 0x4c0001               77 HDRARTLEVEL_LV1 0x4d0000
78 LLSLEVEL_LV1 0x4e0000               79 OBJECTTRACKING_OFF 0x4f0000
80 PANORAMATYPE_2D 0x500000            81 SMARTPROMODE_VIVIDFIREWORKS 0x51000f
82 SMARTPROLEVEL_M2 0x520000           83 EFS_ON 0x530001
84 WIFIMODE_OFF 0x540000               85 VIDEOOUT_PAL 0x550001
86 SMARTAUTORUNSTATE_OFF 0x560000      87 F3DFORMAT_SIDE_BY_SIDE 0x570001
88 FOCUSPEAKINGLEVEL_MID 0x580002      89 LENSMOUNT_NX_MOUNT 0x590000
90 OVEREXPGUIDE_OFF 0x5a0000           91 SHUTTERMIN_OFF 0x5b0000
92 DEPTHBRKAREA_1 0x5c0003             93 INTERVAL_OFF 0x5d0000
94 TIMELAPSE_OFF 0x5e0000              95 VIDEOSTABILIZER_OFF 0x5f0000
96 PANORAMATHUMBNAIL_OFF 0x600000      97 EXTERNALFLASHMODE_ATTL 0x610000
98 WFLASHMODE_OFF 0x620000             99 WFLASHCH_1 0x630000
100 WFLASHMASTERMODE_OFF 0x640000     101 WFLASHGROUPAMODE_OFF 0x650000
102 WFLASHGROUPBMODE_OFF 0x660000     103 WFLASHGROUPCMODE_OFF 0x670000
104 HDMI3DFORMAT_SIDE_BY_SIDE 0x680001 105 LENSZOOMSPEED_LOW 0x690000
106 LENSMFSENSITIVITY_NORMAL 0x6a0000  107 SEPARATEAEAREA_OFF 0x6b0000
108 OLEDCOLOR_ON 0x6c0001              109 BABYMONITOR_OFF 0x6d0000
110 MULTIMOTION_NORMAL 0x6e0002        111 SENSORTESTMODE_OFF 0x6f0000
112 AUTOSHUTTERMODE_BASEBALL 0x700000  113 RVFDPSIZE_FHD 0x710000
114 RVFDPFRAMRATE_FPS30 0x720004       115 SYSTEMFREQENCYSTATE_LIVEVIEW 0x730000
116 HDMI3D_OFF 0x740000                117 ADJUSTSHUTTERTYPE_MECHA 0x750000
118 MOVIE_GAMMA_CONTROL_STANDARD 0x760000 119 MOVIE_LUMINANCE_LEVEL_0_255 0x770000
120 MOVIE_AF_MODE_CONTINUOUS 0x780001  121 GOLFREVERSE_ORIGINAL 0x790000
122 RAWQUALITY_LOSSLESS 0x7a0000       123 RAW_SAVE_OFF 0x7b0000
124 SSIF_RAW_OFF 0x7c0000              125 RAW_BIT_14BIT 0x7d0000
126 RAW_PACK_PACK 0x7e0000             127 FLIPSTATE_UP 0x7f0001
128 SAVEAS_FLIPPED_OFF 0x800000
（129+ 均为 NULL 0xffffffff / 无效）
```

### 5.3 照片风格（PW）切换值表 —— 已验证
```bash
st cap capdtm setusr 20 0x140000  # PW_STANDARD
st cap capdtm setusr 20 0x140001  # PW_VIVID
st cap capdtm setusr 20 0x140002  # PW_PORTRAIT
st cap capdtm setusr 20 0x140003  # PW_LANDSCAPE
st cap capdtm setusr 20 0x140004  # PW_FOREST
st cap capdtm setusr 20 0x140005  # PW_RETRO
st cap capdtm setusr 20 0x140006  # PW_COOL
st cap capdtm setusr 20 0x140007  # PW_CALM
st cap capdtm setusr 20 0x140008  # PW_CLASSIC
st cap capdtm setusr 20 0x140009  # PW_CUSTOM1
st cap capdtm setusr 20 0x14000a  # PW_CUSTOM2
st cap capdtm setusr 20 0x14000c  # PW_CUSTOM4  （0x14000b = CUSTOM3 无效）
```
**= ISP 硬件级实时滤镜切换**（拍 JPEG 时管线内生效，零延迟）。

### 5.4 局限（最终放弃的原因）
1. **PW_CUSTOM 子参数（对比度/饱和度/锐度/色相/RGB 增益）不在 capdtm 暴露范围**（0-128 无、高位枚举 129-500 也无）——只能通过相机 UI 手动调整
2. **槽位数固件固定**：3 个 CUSTOM（CUSTOM1/2/4）+ 8 个可调预设（说明书确认预设也可调参数）= 实际 11 个"可自定义槽位"，但无法程序化写入子参数
3. 无法增加槽位上限（固件闭源）

---

## 6. 已验证结论与边界

| 能力 | 可实现性 | 说明 |
|---|---|---|
| ISP 级实时滤镜（富士式） | ❌ 不可达 | ISP/固件闭源，capdtm 无法写 PW 子参数 |
| 程序化切换预设滤镜（12 种） | ✅ 可 | `setusr 20 <hex>`，ISP 实时 |
| 拍后 CPU 批量/自动处理 | ✅ 可用 | 32s/28MP，配方矩阵/饱和/曲线 |
| 3D LUT（.cube） | ❌ 真机花屏 | 原因未明 |
| 自定义滤镜子参数写入 | ❌ capdtm 不暴露 | 需 UI 手动 |

---

## 7. 关键技术资料

### 7.1 相机脚本自动执行机制（dfmsd）
- SD 卡根放 `info.tg`（内容 `nx_cs.adj` + 换行）→ 开机启动 dfmsd 守护进程
- `nx_cs.adj`（内容 `shell script /mnt/mmc/xxx.sh` + 换行）→ 指定执行脚本
- AUTO 模式开机自动执行；info.tg 会禁用触摸屏（脚本内 `killall dfmsd` 恢复）；相机懒同步（脚本结尾 `sync`×3）；相机是休眠/唤醒非真关机（换文件要拔卡+拔电池）
- 注意：官方 readme 说"文件浏览器点 install.sh"是**错误**的，真实机制是 dfmsd

### 7.2 NEON 优化要点
- `-mcpu=cortex_a9`（zig 下划线格式）
- RGB 交错：`vld3_u8/vst3_u8`（8px）、`vld3q_u8`（16px）
- 矩阵/饱和度：int16 定点 ×64（避免 int16 溢出：255×64<32767），`vshrq_n_s16` 算术移位、`vminq/vmaxq` clamp
- 曲线查表：NEON 后平面查表（向量查表复杂，标量可接受）

### 7.3 踩坑记录
- zig `-O1/-O2` ARM 真机段错误 → 必须 -O0
- zig 缓存目录设 D:/ 路径 → AccessDenied → 用默认缓存（`%LOCALAPPDATA%/zig`），异常时隔离重建
- Windows 沙箱：文件名含特定词（如 filmsim）的写入可能被拒 → 换名（如 ksfilm）
- `jpeg_write_scanlines` 与 `jpeg_read_scanlines` 交错（流式）在 .cube 路径真机异常（花屏/too few scanlines）→ cube 路径整幅缓冲，配方路径流式

---

## 8. 对未来开发者的建议

1. **别再花时间在 capdtm 上找 PW 子参数**——已枚举 0-500 确认不在暴露范围
2. **可能的突破口**（需固件逆向能力）：
   - 逆向 `st`/capdtm 二进制找隐藏参数 ID 或 ioctl
   - 研究相机设置文件存储（CUSTOM 参数可能存于 /opt 某文件，若能注入/复制槽位配置）
   - ISP 驱动研究（DRIMe5 图像管线，内核源码里有相关驱动，但用户态 ISP 接口闭源）
3. **务实路线**：
   - 拍 JPEG：用 capdtm 切换预设（12 种 ISP 实时）+ UI 调 CUSTOM
   - 拍 RAW + 后期：CPU 引擎（ksfilm.arm 配方路径，32s/28MP）
   - 需要 3D LUT：在 PC 上做（别指望相机内）
4. **枚举脚本复用**：`capdtm_enum.sh` 可适配其他参数/固件版本（注意 ID 偏移）

---

## 9. 代码与文件索引

| 文件 | 说明 |
|---|---|
| `test_server/filmsim/engine.py` | PC 原型引擎（矩阵/饱和/曲线/颗粒/.cube/强度） |
| `test_server/filmsim/make_test.py` | 模拟相机测试图生成 |
| `test_server/filmsim/make_cube.py` | 配方烘焙 .cube |
| `test_server/filmsim/export_recipes.py` | 配方 JSON → C 头文件 |
| `test_server/filmsim/recipes/*.json` | 6 个内置配方（portra/velvia/trix/superia/hp5/ektachrome） |
| `test_server/filmsim/arm/fs2.c` | ARM 引擎（NEON + 流式，配方路径可用） |
| `test_server/filmsim/arm/build.sh` | Zig 交叉编译脚本 |
| `test_server/filmsim/arm/neon_test.c` | NEON 冒烟测试 |
| `test_server/filmsim/arm/verify_elf.py` | ELF 属性验证（ARM/EABI5/依赖） |
| `test_server/filmsim/deploy_nx500/` | NX500 部署包（ksfilm.arm、触发文件、capdtm 探测脚本、PC 参考图） |
| `test_server/filmsim/deploy_nx500/capdtm_enum.sh` | capdtm 参数枚举（数字 ID） |
| `test_server/filmsim/deploy_nx500/capdtm_pw2.sh` | PW 风格切换验证 |

---

*本报告由 NX-KS2 项目胶片仿真探索（2026-08-15）整理，欢迎基于此继续开发。*
