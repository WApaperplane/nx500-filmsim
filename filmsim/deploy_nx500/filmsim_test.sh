#!/bin/bash
# NX500 胶片引擎 v6 测试脚本（整数定点提速版 + 耗时统计）
# 1) test.JPG 回归 2) real.JPG 全分辨率（配方 + LUT 速度对比）
cd /mnt/mmc || { echo "no /mnt/mmc" > /mnt/mmc/filmsim.log; exit 1; }
killall dfmsd 2>/dev/null

LOG=/mnt/mmc/filmsim.log
echo "[ksfilm] start $(date)" > "$LOG"
chmod +x /mnt/mmc/ksfilm.arm 2>/dev/null

# ---- 0) NEON 冒烟测试（验证 NEON 指令可用性） ----
echo "--- neon smoke ---" >> "$LOG"
/mnt/mmc/neon_test.arm >> "$LOG" 2>&1
echo "neon_exit=$?" >> "$LOG"

run_one() {
    local in="$1" out="$2" rid="$3" extra="$4"
    local t0 t1 ec
    t0=$(date +%s)
    echo "--- $rid $extra ---" >> "$LOG"
    /mnt/mmc/ksfilm.arm "$in" "$out" "$rid" $extra >> "$LOG" 2>&1
    ec=$?
    t1=$(date +%s)
    echo "exit=$ec 耗时=$((t1-t0))s" >> "$LOG"
}

# ---- 1) 测试图回归（快速） ----
for pair in "portra400:out_portra.JPG" "velvia50:out_velvia.JPG" "trix400:out_trix.JPG" \
            "superia400:out_superia.JPG" "hp5:out_hp5.JPG" "ektachrome:out_ektachrome.JPG"; do
    rid="${pair%%:*}"; out="${pair##*:}"
    run_one /mnt/mmc/test.JPG /mnt/mmc/$out $rid
done

# ---- 2) real 全分辨率（配方路径） ----
if [ -f /mnt/mmc/real.JPG ]; then
    run_one /mnt/mmc/real.JPG /mnt/mmc/real_portra.JPG portra400
    run_one /mnt/mmc/real.JPG /mnt/mmc/real_trix.JPG trix400
fi

sync; sync; sync
echo "[ksfilm] done. 拔卡查看输出与 filmsim.log（含耗时）" >> "$LOG"
