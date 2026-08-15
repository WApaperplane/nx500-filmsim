#!/bin/bash
# capdtm PW 切换实验：验证 setusr 20 能否程序化切换照片风格（快，12 次测试）
# 目标：找出 PW_STANDARD/VIVID/.../CUSTOM1-4 的 hex 值
LOG=/mnt/mmc/capdtm_pw.log
echo "=== pw test $(date) ===" > "$LOG"
echo "当前: $(st cap capdtm getusr 20)" >> "$LOG"
for v in 1 2 3 4 5 6 7 8 9 10 11 12; do
    hexv=$(printf '%02x' $v)
    st cap capdtm setusr 20 0x14${hexv}0000 >> "$LOG" 2>&1
    echo "setusr 20 0x14${hexv}0000 -> $(st cap capdtm getusr 20)" >> "$LOG"
done
st cap capdtm setusr 20 0x140000 >> "$LOG" 2>&1
echo "恢复标准: $(st cap capdtm getusr 20)" >> "$LOG"
sync; sync; sync
echo "=== pw test done. 拔卡查看 capdtm_pw.log ===" >> "$LOG"
