#!/bin/bash
# capdtm 探测脚本：找出 Picture Wizard 自定义参数名
# 用法：放 SD 卡根，info.tg/nx_cs.adj 指向它，开机自动跑（或手动执行）
LOG=/mnt/mmc/capdtm_probe.log
echo "=== capdtm probe $(date) ===" > "$LOG"

# 1) 无参数 getusr：尝试列出全部参数名
echo "--- getusr (无参数) ---" >> "$LOG"
st cap capdtm getusr >> "$LOG" 2>&1

# 2) 当前照片风格
echo "--- USERDATA_PW ---" >> "$LOG"
st cap capdtm getusr USERDATA_PW >> "$LOG" 2>&1

# 3) 探测 PW_CUSTOM 子参数名（常见命名变体）
for p in \
  USERDATA_PW_CUSTOM1_CONTRAST USERDATA_PWC1_CONTRAST USERDATA_PW_C1_CONTRAST \
  USERDATA_PW_CUSTOM1_SATURATION USERDATA_PWC1_SATURATION \
  USERDATA_PW_CUSTOM1_SHARPNESS USERDATA_PWC1_SHARPNESS \
  USERDATA_PW_CUSTOM1_HUE USERDATA_PWC1_HUE \
  USERDATA_PW_CUSTOM1_COLOR USERDATA_PWC1_COLOR \
  USERDATA_PW_CUSTOM1_R USERDATA_PWC1_R USERDATA_PW_CUSTOM1_G USERDATA_PWC1_G \
  USERDATA_PW_CUSTOM1_B USERDATA_PWC1_B \
  PW_CUSTOM1_CONTRAST PWC1_CONTRAST PWC1_SATURATION PWC1_SHARPNESS \
  PWC1_HUE PWC1_COLOR PWC1_R PWC1_G PWC1_B \
  CONTRAST SATURATION SHARPNESS HUE COLOR \
  USERDATA_CONTRAST USERDATA_SATURATION USERDATA_SHARPNESS USERDATA_HUE; do
  echo "--- $p ---" >> "$LOG"
  st cap capdtm getusr "$p" >> "$LOG" 2>&1
done

# 4) 验证写入：切到标准再切回自定义1
echo "--- setusr USERDATA_PW PW_STANDARD ---" >> "$LOG"
st cap capdtm setusr USERDATA_PW PW_STANDARD >> "$LOG" 2>&1
echo "exit=$?" >> "$LOG"
echo "--- setusr USERDATA_PW PW_CUSTOM1 ---" >> "$LOG"
st cap capdtm setusr USERDATA_PW PW_CUSTOM1 >> "$LOG" 2>&1
echo "exit=$?" >> "$LOG"

sync; sync; sync
echo "=== probe done. 拔卡查看 capdtm_probe.log ===" >> "$LOG"
