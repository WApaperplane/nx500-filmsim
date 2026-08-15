#!/bin/bash
# capdtm PW 切换实验 v2：修正 hex（低 16 位是值）
# setusr 20 0x140001..0x14000C（值 1-12），确认各风格名
LOG=/mnt/mmc/capdtm_pw2.log
echo "=== pw test v2 $(date) ===" > "$LOG"
echo "当前: $(st cap capdtm getusr 20)" >> "$LOG"
for v in 1 2 3 4 5 6 7 8 9 10 11 12; do
    hexv=$(printf '%04x' $v)
    st cap capdtm setusr 20 0x14${hexv} >> "$LOG" 2>&1
    echo "setusr 20 0x14${hexv} -> $(st cap capdtm getusr 20)" >> "$LOG"
done
st cap capdtm setusr 20 0x140000 >> "$LOG" 2>&1
echo "恢复标准: $(st cap capdtm getusr 20)" >> "$LOG"
sync; sync; sync
echo "=== pw2 done. 拔卡查看 capdtm_pw2.log ===" >> "$LOG"
