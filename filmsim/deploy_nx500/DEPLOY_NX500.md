# NX500 胶片引擎阶段 2B+ 部署指南（打印质量版 v5）

> 你的环境：NX500 固件 1.12 + dfmsd 自动触发机制（已打通）
> v5 画质修复：**全分辨率处理 + JPEG 4:4:4 无色度抽样 + quality 95**（修复 v4 的 810px 缩图 + 4:2:0 色彩减半问题）

## 一、部署包内容（`deploy_nx500/`）

| 文件 | 作用 |
|---|---|
| `info.tg` + `nx_cs.adj` | dfmsd 触发器 |
| `filmsim_test.sh` | 开机自动测试：test 回归 + real 全分辨率 |
| `ksfilm.arm` | ★ 引擎 v5：全分辨率 + 4:4:4 + q95 + `--grain` 可调 |
| `test.JPG` | 测试照片（**记得一起拷**） |
| `velvia50.cube` | 示例 3D LUT |
| `pc_ref/` | PC 参考输出 |

## 二、部署步骤
1. 拷 **info.tg、nx_cs.adj、filmsim_test.sh、ksfilm.arm、test.JPG、velvia50.cube** 到 SD 卡根
2. AUTO → 插卡开机 → 等 1~3 分钟（real 全分辨率较慢）→ 关机拔卡

## 三、画质说明（v5 关键改进）
| 问题（v4） | 修复（v5） |
|---|---|
| 真实照片被缩到 810px（libjpeg 整数缩放丢细节） | **全分辨率处理**（不缩图） |
| JPEG 默认 4:2:0 色度抽样（色彩细节减半） | **4:4:4 无抽样** |
| quality 92 | **quality 95**（打印标准） |
| 颗粒强度不可调 | **`--grain <0..2>` 参数覆盖**（0=无颗粒，1=配方默认） |

## 四、ksfilm 用法
```
/mnt/mmc/ksfilm.arm <in> <out> <recipe_id> [--lut <file.cube>] [--max <px>] [--grain <0..2>]
# recipe: portra400 | velvia50 | trix400 | superia400 | hp5 | ektachrome
# --max: 可选缩图（默认全分辨率）；--grain: 覆盖颗粒强度
```
示例（全分辨率 + 颗粒减半）：
```
/mnt/mmc/ksfilm.arm /mnt/mmc/real.JPG /mnt/mmc/real_portra.JPG portra400 --grain 0.5
```

## 五、真实照片测试（B 方案）
1. 拍一张真实照片 → 拷 SD 卡根命名 `real.JPG`
2. 开机自动处理：`real_portra.JPG`（暖调）+ `real_trix.JPG`（黑白）——**全分辨率 4:4:4**
3. 速度：约 1~2 分钟/张（-O0 + 全像素，正常；要快可 `--max 3000`）

## 六、注意事项
- 换文件要拔卡+拔电池；info.tg 触摸屏短暂失灵（自动恢复）；验证完删 info.tg
- 打印建议：输出已是 4:4:4 q95 全分辨率，可直接打印

- 换文件要**拔卡+拔电池**；info.tg 会让触摸屏短暂失灵（脚本自动恢复）；验证完删 info.tg
- `.cube` 文件放 SD 卡根，路径参数给 `/mnt/mmc/xxx.cube`
- 6 配方处理约 30~40 秒（-O0 无优化，正常；真机实拍照片更大时更久）

## 七、紧急救砖（NX500 免拆机恢复）

若遇到软砖无法正常开机：将官方未修改 `nx500.bin` 放入干净的 FAT32 SD 卡根目录，拔电池插卡并把屏幕完全翻起至自拍位，装回电池后按住 **MOBILE (Wi-Fi) + AEL + 全按快门** 开机，进入蓝底 Recovery Mode 后长按快门重刷（刷写中严禁断电，直至 `Update Finish`；已在 NX500 实机验证，NX1 未验证）。
