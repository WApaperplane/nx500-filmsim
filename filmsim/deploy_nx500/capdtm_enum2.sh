#!/bin/bash
# capdtm 第二轮探测：扩展高位枚举 + PW 照片风格切换实验
# 目标：找到 Picture Wizard 子参数（对比度/饱和度/锐度/色相/RGB增益）和 PW 取值 hex
LOG=/mnt/mmc/capdtm_enum2.log
echo "=== capdtm enum2 $(date) ===" > "$LOG"

# 1) 高位枚举 129..500（跳过 NULL 段，找子参数）
echo "--- 高位枚举 129..500 ---" >> "$LOG"
for id in $(seq 129 500); do
    out=$(st cap capdtm getusr $id 2>&1)
    case "$out" in
        *"UserData"*"NULL"*) ;;
        *"UserData"*) echo "$id | $out" >> "$LOG" ;;
    esac
done

# 2) PW 取值实验：切不同风格后 getusr 20 确认值格式
echo "--- PW 取值实验 ---" >> "$LOG"
echo "当前: $(st cap capdtm getusr 20)" >> "$LOG"
for val in 0x140001 0x140009 0x14000C; do
    st cap capdtm setusr 20 $val >> "$LOG" 2>&1
    echo "setusr 20 $val -> $(st cap capdtm getusr 20)" >> "$LOG"
done
# 恢复为标准
st cap capdtm setusr 20 0x140000 >> "$LOG" 2>&1
echo "恢复: $(st cap capdtm getusr 20)" >> "$LOG"

sync; sync; sync
echo "=== enum2 done. 拔卡查看 capdtm_enum2.log ===" >> "$LOG"
