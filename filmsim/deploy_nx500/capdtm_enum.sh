#!/bin/bash
# capdtm 参数枚举脚本：枚举数字 ID，收集全部有效参数名（含 Picture Wizard）
# 用法：放 SD 卡根，nx_cs.adj 指向它，开机自动跑
LOG=/mnt/mmc/capdtm_enum.log
echo "=== capdtm enum $(date) ===" > "$LOG"
echo "--- 枚举 getusr 0..140 ---" >> "$LOG"
for id in $(seq 0 140); do
    out=$(st cap capdtm getusr $id 2>&1)
    case "$out" in
        *"UserData"*) echo "$id | $out" >> "$LOG" ;;
    esac
done
sync; sync; sync
echo "=== enum done. 拔卡查看 capdtm_enum.log ===" >> "$LOG"
