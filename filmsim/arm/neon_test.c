#include <arm_neon.h>
#include <stdio.h>

int main(void) {
    /* 最小 NEON 冒烟测试：验证 NX500 上 NEON 指令可用 */
    uint8x8_t a = vdup_n_u8(3);
    uint8x8_t b = vadd_u8(a, a);
    uint16x4_t c = vpaddl_u8(b);
    int sum = vget_lane_u16(c, 0) + vget_lane_u16(c, 1) + vget_lane_u16(c, 2) + vget_lane_u16(c, 3);
    printf("[nx-ks2] NEON smoke test: 8x3 -> %d (expect 24)\n", sum);
    return sum == 24 ? 0 : 1;
}
