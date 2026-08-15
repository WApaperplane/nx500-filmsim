# On-Camera Film Simulation Research (NX500/NX1)

> This document records the complete exploration of "in-camera film simulation / filters" on a Samsung NX500 (firmware 1.12, DRIMe5 platform) performed on 2026-08-15: verified findings, key technical references, and failure lessons — so future developers don't repeat the same dead ends.
>
> 中文版见 [FILM_SIM_RESEARCH.zh.md](FILM_SIM_RESEARCH.zh.md)

---

## Table of Contents
1. [Background & Goal](#1-background--goal)
2. [Three Technical Routes Overview](#2-three-technical-routes-overview)
3. [Route A: CPU Software Engine (Post-Processing) — WORKING](#3-route-a-cpu-software-engine-post-processing--working)
4. [Route B: 3D LUT (.cube) — ABANDONED](#4-route-b-3d-lut-cube--abandoned)
5. [Route C: Samsung ISP Filter (capdtm) — Partially Working, Abandoned](#5-route-c-samsung-isp-filter-capdtm--partially-working-abandoned)
6. [Verified Conclusions & Boundaries](#6-verified-conclusions--boundaries)
7. [Key Technical References](#7-key-technical-references)
8. [Suggestions for Future Developers](#8-suggestions-for-future-developers)
9. [Code & File Index](#9-code--file-index)

---

## 1. Background & Goal

- **Platform**: Samsung NX500 camera (firmware 1.12, Tizen 2.2, DRIMe5 SoC = ARM Cortex-A9 dual-core, NEON available)
- **Existing base**: NX-KS2 mod system (SD-card script auto-execution, bluetoothd hook, Web remote control, etc.)
- **Goal**: implement a Fuji-film-simulation / Panasonic-LUT-style "in-camera filter" — ideally **real-time at ISP level** when shooting JPEG; fallback: post-processing

---

## 2. Three Technical Routes Overview

| Route | Approach | Result | Conclusion |
|---|---|---|---|
| A | CPU software engine (decode → process → encode) | ✅ Full-res 28MP works (32s/image) | Usable, not real-time |
| B | 3D LUT (.cube lookup + interpolation) | ❌ Persistent artifacts on camera (root cause unknown) | Abandoned |
| C | Samsung ISP filter (capdtm parameters) | ⚠️ Can switch presets/slots, but cannot write custom sub-parameters | Partially working, abandoned |

**Bottom line: because the ISP/firmware is closed-source, "ISP-level real-time custom filters when shooting JPEG" is NOT achievable on NX500.** The CPU engine remains a viable batch/automatic post-processing solution.

---

## 3. Route A: CPU Software Engine (Post-Processing) — WORKING

### 3.1 Architecture
```
PC prototype (Python/Pillow)
    ↓ algorithm validation
ARM cross-compile (Zig → arm-linux-gnueabi.2.15)
    ↓
Camera deployment (SD card info.tg + nx_cs.adj → dfmsd auto-run)
    ↓
Pipeline: JPEG decode → color matrix → saturation → curve LUT → (grain removed) → JPEG encode (4:4:4 q95)
```

### 3.2 Cross-compile toolchain (verified, reproducible)
- **Toolchain**: Zig 0.13.0 (single Windows binary, built-in glibc cross-compilation)
- **Target triple**: `arm-linux-gnueabi.2.15` (Tizen 2.2 glibc ~2.15, EABI5 softfp — confirmed by parsing rootfs ELF flags)
- **NEON**: `-mcpu=cortex_a9` (zig uses underscore format; hyphen format rejected)
- **Key flags**:
  ```
  zig cc -target arm-linux-gnueabi.2.15 -O0 -mcpu=cortex_a9 \
         -mfloat-abi=soft -fno-stack-protector \
         -I<rootfs>/usr/include \
         filmsim.c -L<rootfs>/usr/lib -ljpeg -o filmsim.arm
  ```
- **⚠️ MUST use -O0**: `-O1/-O2` compiled ARM code segfaults on the NX500 (exit 139), regardless of integer-ization (compiler codegen issue)
- **System libs**: use the camera rootfs's own `libjpeg-turbo` (`libjpeg.so.8`, deps only libc+libgcc_s)
- **Avoid linking libm**: under softfp, double ops go through software float emulation (very slow); also zig's glibc libm stub build may hit sandbox AccessDenied → **hand-implement math** (exp via series, Gaussian noise via central limit theorem) or use integer fixed-point

### 3.3 Performance ladder (28MP full-res 6480×4320)
| Version | Approach | Time |
|---|---|---|
| v6 | double float | 60-120s |
| v8 | integer fixed-point (matrix ×1024/×64) | 68s |
| v10 | + NEON 8px (vld3/vst3) | 37s |
| v11 | + curve merged single pass | 34s |
| v12 | + NEON 16px + streaming pipeline | 32s |

- **Bottlenecks**: JPEG encode (4:4:4 q95 28MP ≈ 40%), pixel processing (NEON ≈ 25%), decode (libjpeg-turbo already NEON), SD I/O
- **Conclusion**: 32s/image is the practical limit of the CPU software pipeline (dual-core parallel risky, limited gain)

### 3.4 Streaming pipeline notes
- Decode one row → process → encode one row (zero full-frame buffer; 28MP memory drops from 84MB to ~15MB)
- But **streaming .cube LUT on real camera misbehaved** (see Route B)

---

## 4. Route B: 3D LUT (.cube) — ABANDONED

### 4.1 What was done
- PC: `.cube` parser (LUT_3D_SIZE + data rows) + trilinear interpolation (integer fixed-point: coords ×256, frac 0-255, LUT stored as 0-255 ints)
- ARM: `--lut <file.cube>` option; recipe-to-LUT baking tool (make_cube.py)
- **PC validation: integer vs float average diff 1.06/255 (visually identical) — algorithm is correct**

### 4.2 Real-camera symptom (reproduced v8 → v14)
- **All versions (full-buffer / streaming / various fixes) produced corrupted images on camera** (misaligned rows, color chaos, abnormally large files)
- exit=0 but output corrupt — algorithm correct, runtime wrong, **root cause never isolated** (suspected binary/runtime memory issue)

### 4.3 Lesson
- PC algorithm validation ≠ real camera runs (especially -O0 ARM + large 84MB buffer scenarios)
- Debugging cost is extreme (every iteration needs card removal + camera test) — cut losses early

---

## 5. Route C: Samsung ISP Filter (capdtm) — Partially Working, Abandoned

### 5.1 Key discovery: capdtm uses **numeric IDs** (not names)
```bash
# Wrong (names invalid):
st cap capdtm getusr USERDATA_PW        # → Invalid argument
# Correct (numeric ID):
st cap capdtm getusr 20                  # → UserData is PW_STANDARD (0x140000)
st cap capdtm setusr 20 0x140002         # → UserData is PW_PORTRAIT
```
- Hex format: **high 16 bits = parameter group, low 16 bits = value** (e.g. COLORSPACE=0x300002: group 0x30, value 0x0002)
- Note: **NX500 and NX1 have different ID offsets** (NX1: 48=COLORSPACE; NX500: 50=COLORSPACE) — enumerate per camera

### 5.2 NX500 full capdtm parameter table (enum 0-128, firmware 1.12)
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
20 PW_STANDARD 0x140000   ★Picture Wizard 21 SMARTRANGE_ON 0x150001
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
(129+ are NULL 0xffffffff / invalid)
```

### 5.3 Picture Wizard (PW) switch value table — VERIFIED
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
st cap capdtm setusr 20 0x14000c  # PW_CUSTOM4  (0x14000b = CUSTOM3 invalid)
```
**= ISP-level real-time filter switching** (applies in the JPEG pipeline, zero latency).

### 5.4 Limitations (why it was abandoned)
1. **PW_CUSTOM sub-parameters (contrast/saturation/sharpness/hue/RGB gain) are NOT exposed by capdtm** (absent in 0-128; high-ID enum 129-500 also absent) — can only be adjusted via camera UI manually
2. **Slot count is fixed by firmware**: 3 CUSTOM (CUSTOM1/2/4) + 8 adjustable presets (manual confirms presets are also adjustable) = practically 11 "customizable slots", but sub-parameters cannot be written programmatically
3. Cannot increase slot count (closed-source firmware)

---

## 6. Verified Conclusions & Boundaries

| Capability | Feasible | Notes |
|---|---|---|
| ISP-level real-time filter (Fuji-style) | ❌ Not reachable | ISP/firmware closed-source; capdtm cannot write PW sub-parameters |
| Programmatic preset switching (12 styles) | ✅ Yes | `setusr 20 <hex>`, ISP real-time |
| CPU post-processing (batch/auto) | ✅ Usable | 32s/28MP, matrix/saturation/curve recipes |
| 3D LUT (.cube) | ❌ Artifacts on camera | Root cause unknown |
| Custom filter sub-parameter write | ❌ Not exposed | UI-only |

---

## 7. Key Technical References

### 7.1 SD-card script auto-execution (dfmsd)
- Put `info.tg` on SD card root (content `nx_cs.adj` + newline) → dfmsd daemon starts at boot
- `nx_cs.adj` (content `shell script /mnt/mmc/xxx.sh` + newline) → specifies script to run
- Auto-runs at boot in AUTO mode; info.tg disables touchscreen (script should `killall dfmsd` to restore); camera syncs lazily (end scripts with `sync`×3); camera hibernates rather than true power-off (remove card AND battery when swapping files)
- Note: the official README's "file browser tap install.sh" claim is WRONG — the real mechanism is dfmsd

### 7.2 NEON optimization notes
- `-mcpu=cortex_a9` (zig underscore format)
- RGB interleaved: `vld3_u8/vst3_u8` (8px), `vld3q_u8` (16px)
- Matrix/saturation: int16 fixed-point ×64 (avoid overflow: 255×64<32767), `vshrq_n_s16` arithmetic shift, `vminq/vmaxq` clamp
- Curve lookup: plane-wise scalar after NEON (vectorized lookup too complex; scalar acceptable)

### 7.3 Gotchas
- zig `-O1/-O2` ARM segfaults on camera → must use -O0
- zig cache dir set to D:/ path → AccessDenied → use default cache (`%LOCALAPPDATA%/zig`), isolate/rebuild when corrupted
- Windows sandbox: writing files whose names contain certain words (e.g. "filmsim") may be denied → rename (e.g. ksfilm)
- Interleaving `jpeg_write_scanlines` with `jpeg_read_scanlines` (streaming) misbehaved for the .cube path on camera (artifacts/"too few scanlines") → keep .cube path full-buffer; streaming is fine for the recipe path

---

## 8. Suggestions for Future Developers

1. **Don't waste time looking for PW sub-parameters in capdtm** — enumerated 0-500, confirmed not exposed
2. **Potential breakthroughs** (require firmware reverse-engineering):
   - Reverse-engineer the `st`/capdtm binary for hidden parameter IDs or ioctls
   - Study camera settings storage (CUSTOM parameters may live in a file under /opt; if injectable/duplicable, slot configs could be cloned)
   - ISP driver research (DRIMe5 image pipeline; kernel sources exist, but user-space ISP API is closed)
3. **Pragmatic routes**:
   - Shooting JPEG: switch presets via capdtm (12 ISP real-time styles) + adjust CUSTOM in UI
   - RAW + post: CPU engine (ksfilm.arm recipe path, 32s/28MP)
   - If you need 3D LUT: do it on a PC (don't expect it in-camera)
4. **Reuse the enum script**: `capdtm_enum.sh` adapts to other parameters/firmware versions (watch for ID offsets)

---

## 9. Code & File Index

| File | Description |
|---|---|
| `test_server/filmsim/engine.py` | PC prototype engine (matrix/saturation/curve/grain/.cube/strength) |
| `test_server/filmsim/make_test.py` | Synthetic camera test image generator |
| `test_server/filmsim/make_cube.py` | Recipe → .cube baking |
| `test_server/filmsim/export_recipes.py` | Recipe JSON → C header |
| `test_server/filmsim/recipes/*.json` | 6 built-in recipes (portra/velvia/trix/superia/hp5/ektachrome) |
| `test_server/filmsim/arm/fs2.c` | ARM engine (NEON + streaming; recipe path works) |
| `test_server/filmsim/arm/build.sh` | Zig cross-compile script |
| `test_server/filmsim/arm/neon_test.c` | NEON smoke test |
| `test_server/filmsim/arm/verify_elf.py` | ELF attribute check (ARM/EABI5/deps) |
| `test_server/filmsim/deploy_nx500/` | NX500 deploy package (ksfilm.arm, trigger files, capdtm probe scripts, PC reference images) |
| `test_server/filmsim/deploy_nx500/capdtm_enum.sh` | capdtm parameter enumeration (numeric IDs) |
| `test_server/filmsim/deploy_nx500/capdtm_pw2.sh` | PW style switch verification |

---

*Compiled from the NX-KS2 film simulation exploration (2026-08-15). Feel free to build on it.*
