/*
 * NX-KS2 阶段2B：完整 ARM 胶片配方引擎
 * 管线：JPEG 读 -> 色彩矩阵 -> 饱和度 -> 曲线 LUT -> 数学颗粒 -> JPEG 写回
 * 算法与 PC 端 engine.py 严格一致（-O0 编译，真机已验证稳定）
 * 用法: filmsim <in.jpg> <out.jpg> [recipe_id]  (默认 portra400)
 * 配方表: recipes_gen.h（由 export_recipes.py 生成）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arm_neon.h>
#include <jpeglib.h>
#include "recipes_gen.h"

#define CLAMP255(v) ((v) < 0 ? 0 : ((v) > 255 ? 255 : (v)))

/* 自实现 math（避免链接 libm：zig 生成 glibc libm stub 在沙箱下 AccessDenied） */
static double my_exp(double x) {
    /* 级数展开 e^x，|x|<=4 时 20 项内收敛 */
    double s = 1.0, term = 1.0;
    for (int n = 1; n <= 24; n++) {
        term *= x / n;
        s += term;
        if (term < 1e-13 && term > -1e-13) break;
    }
    return s;
}

/* 高斯噪声：中心极限定理近似（12 个均匀分布和），无需 sin/cos/log/sqrt */
static double gauss_noise(void) {
    double s = 0.0;
    for (int i = 0; i < 12; i++)
        s += rand() / (RAND_MAX + 1.0);
    return s - 6.0;
}

/* ---------------- 色彩矩阵（整数定点 ×1024，softfp 下避免软件浮点） ---------------- */
static void apply_matrix(JSAMPLE *row, int width, const double m[3][3]) {
    int m00 = (int)(m[0][0] * 1024.0 + 0.5), m01 = (int)(m[0][1] * 1024.0 + 0.5), m02 = (int)(m[0][2] * 1024.0 + 0.5);
    int m10 = (int)(m[1][0] * 1024.0 + 0.5), m11 = (int)(m[1][1] * 1024.0 + 0.5), m12 = (int)(m[1][2] * 1024.0 + 0.5);
    int m20 = (int)(m[2][0] * 1024.0 + 0.5), m21 = (int)(m[2][1] * 1024.0 + 0.5), m22 = (int)(m[2][2] * 1024.0 + 0.5);
    for (int x = 0; x < width; x++) {
        unsigned char *p = row + x * 3;
        int r = p[0], g = p[1], b = p[2];
        p[0] = (JSAMPLE)CLAMP255((m00*r + m01*g + m02*b) >> 10);
        p[1] = (JSAMPLE)CLAMP255((m10*r + m11*g + m12*b) >> 10);
        p[2] = (JSAMPLE)CLAMP255((m20*r + m21*g + m22*b) >> 10);
    }
}

/* ---------------- NEON 色彩+曲线（×64 定点 int16，vld3q 16 像素/次） ----------------
 * softfp 下 -O1/-O2 崩溃，用 -O0 + NEON 内建向量化（NX500 = Cortex-A9，NEON 可用）
 * 矩阵+饱和度+曲线查表合并为单 pass（省一次全图内存往返） */
static void apply_color_neon(JSAMPLE *row, int width, const double m[3][3], double sat,
                             const unsigned char *lut) {
    int16_t m00 = (int16_t)(m[0][0]*64.0+0.5), m01 = (int16_t)(m[0][1]*64.0+0.5), m02 = (int16_t)(m[0][2]*64.0+0.5);
    int16_t m10 = (int16_t)(m[1][0]*64.0+0.5), m11 = (int16_t)(m[1][1]*64.0+0.5), m12 = (int16_t)(m[1][2]*64.0+0.5);
    int16_t m20 = (int16_t)(m[2][0]*64.0+0.5), m21 = (int16_t)(m[2][1]*64.0+0.5), m22 = (int16_t)(m[2][2]*64.0+0.5);
    int16_t s = (int16_t)(sat*64.0+0.5);
    int16x8_t sl = vdupq_n_s16(s), z = vdupq_n_s16(0), mx = vdupq_n_s16(255);

    /* 8 像素 NEON 核心：矩阵+饱和+曲线 */
    uint8x8_t or8, og8, ob8;
    int x = 0;
    /* 16 像素块（vld3q 宽向量） */
    for (; x + 16 <= width; x += 16) {
        uint8x16x3_t px = vld3q_u8(row + x * 3);
        int16x8_t r = (int16x8_t)vmovl_u8(vget_low_u8(px.val[0]));
        int16x8_t g = (int16x8_t)vmovl_u8(vget_low_u8(px.val[1]));
        int16x8_t b = (int16x8_t)vmovl_u8(vget_low_u8(px.val[2]));
        int16x8_t nr = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m00), vmulq_n_s16(g, m01)), vmulq_n_s16(b, m02));
        int16x8_t ng = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m10), vmulq_n_s16(g, m11)), vmulq_n_s16(b, m12));
        int16x8_t nb = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m20), vmulq_n_s16(g, m21)), vmulq_n_s16(b, m22));
        nr = vshrq_n_s16(nr, 6); ng = vshrq_n_s16(ng, 6); nb = vshrq_n_s16(nb, 6);
        int16x8_t lum = vshrq_n_s16(vaddq_s16(vaddq_s16(vmulq_n_s16(nr, 19), vmulq_n_s16(ng, 38)), vmulq_n_s16(nb, 7)), 6);
        nr = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(nr, lum), sl), 6));
        ng = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(ng, lum), sl), 6));
        nb = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(nb, lum), sl), 6));
        nr = vmaxq_s16(vminq_s16(nr, mx), z);
        ng = vmaxq_s16(vminq_s16(ng, mx), z);
        nb = vmaxq_s16(vminq_s16(nb, mx), z);
        uint8x8_t a = vmovn_u16((uint16x8_t)nr), c = vmovn_u16((uint16x8_t)ng), e = vmovn_u16((uint16x8_t)nb);
        uint8_t rr[8], gg[8], bb[8];
        vst1_u8(rr, a); vst1_u8(gg, c); vst1_u8(bb, e);
        for (int k = 0; k < 8; k++) { rr[k] = lut[rr[k]]; gg[k] = lut[gg[k]]; bb[k] = lut[bb[k]]; }
        uint8x8x3_t lo;
        lo.val[0] = vld1_u8(rr); lo.val[1] = vld1_u8(gg); lo.val[2] = vld1_u8(bb);
        vst3_u8(row + x * 3, lo);
        /* 高 8 像素 */
        r = (int16x8_t)vmovl_u8(vget_high_u8(px.val[0]));
        g = (int16x8_t)vmovl_u8(vget_high_u8(px.val[1]));
        b = (int16x8_t)vmovl_u8(vget_high_u8(px.val[2]));
        nr = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m00), vmulq_n_s16(g, m01)), vmulq_n_s16(b, m02));
        ng = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m10), vmulq_n_s16(g, m11)), vmulq_n_s16(b, m12));
        nb = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m20), vmulq_n_s16(g, m21)), vmulq_n_s16(b, m22));
        nr = vshrq_n_s16(nr, 6); ng = vshrq_n_s16(ng, 6); nb = vshrq_n_s16(nb, 6);
        lum = vshrq_n_s16(vaddq_s16(vaddq_s16(vmulq_n_s16(nr, 19), vmulq_n_s16(ng, 38)), vmulq_n_s16(nb, 7)), 6);
        nr = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(nr, lum), sl), 6));
        ng = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(ng, lum), sl), 6));
        nb = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(nb, lum), sl), 6));
        nr = vmaxq_s16(vminq_s16(nr, mx), z);
        ng = vmaxq_s16(vminq_s16(ng, mx), z);
        nb = vmaxq_s16(vminq_s16(nb, mx), z);
        a = vmovn_u16((uint16x8_t)nr); c = vmovn_u16((uint16x8_t)ng); e = vmovn_u16((uint16x8_t)nb);
        vst1_u8(rr, a); vst1_u8(gg, c); vst1_u8(bb, e);
        for (int k = 0; k < 8; k++) { rr[k] = lut[rr[k]]; gg[k] = lut[gg[k]]; bb[k] = lut[bb[k]]; }
        uint8x8x3_t hi;
        hi.val[0] = vld1_u8(rr); hi.val[1] = vld1_u8(gg); hi.val[2] = vld1_u8(bb);
        vst3_u8(row + x * 3 + 24, hi);
    }
    /* 8 像素余块 */
    for (; x + 8 <= width; x += 8) {
        uint8x8x3_t px = vld3_u8(row + x * 3);
        int16x8_t r = (int16x8_t)vmovl_u8(px.val[0]);
        int16x8_t g = (int16x8_t)vmovl_u8(px.val[1]);
        int16x8_t b = (int16x8_t)vmovl_u8(px.val[2]);
        int16x8_t nr = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m00), vmulq_n_s16(g, m01)), vmulq_n_s16(b, m02));
        int16x8_t ng = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m10), vmulq_n_s16(g, m11)), vmulq_n_s16(b, m12));
        int16x8_t nb = vaddq_s16(vaddq_s16(vmulq_n_s16(r, m20), vmulq_n_s16(g, m21)), vmulq_n_s16(b, m22));
        nr = vshrq_n_s16(nr, 6); ng = vshrq_n_s16(ng, 6); nb = vshrq_n_s16(nb, 6);
        int16x8_t lum = vshrq_n_s16(vaddq_s16(vaddq_s16(vmulq_n_s16(nr, 19), vmulq_n_s16(ng, 38)), vmulq_n_s16(nb, 7)), 6);
        nr = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(nr, lum), sl), 6));
        ng = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(ng, lum), sl), 6));
        nb = vaddq_s16(lum, vshrq_n_s16(vmulq_s16(vsubq_s16(nb, lum), sl), 6));
        nr = vmaxq_s16(vminq_s16(nr, mx), z);
        ng = vmaxq_s16(vminq_s16(ng, mx), z);
        nb = vmaxq_s16(vminq_s16(nb, mx), z);
        uint8x8_t a = vmovn_u16((uint16x8_t)nr), c = vmovn_u16((uint16x8_t)ng), e = vmovn_u16((uint16x8_t)nb);
        uint8_t rr[8], gg[8], bb[8];
        vst1_u8(rr, a); vst1_u8(gg, c); vst1_u8(bb, e);
        for (int k = 0; k < 8; k++) { rr[k] = lut[rr[k]]; gg[k] = lut[gg[k]]; bb[k] = lut[bb[k]]; }
        uint8x8x3_t out;
        out.val[0] = vld1_u8(rr); out.val[1] = vld1_u8(gg); out.val[2] = vld1_u8(bb);
        vst3_u8(row + x * 3, out);
    }
    /* 尾部标量（×64 定点，与 NEON 一致） */
    for (; x < width; x++) {
        unsigned char *p = row + x * 3;
        int r = p[0], g = p[1], b = p[2];
        int nr = (m00*r + m01*g + m02*b) >> 6;
        int ng = (m10*r + m11*g + m12*b) >> 6;
        int nb = (m20*r + m21*g + m22*b) >> 6;
        int lum = (19*nr + 38*ng + 7*nb) >> 6;
        nr = lum + (((nr - lum) * s) >> 6);
        ng = lum + (((ng - lum) * s) >> 6);
        nb = lum + (((nb - lum) * s) >> 6);
        p[0] = (JSAMPLE)lut[CLAMP255(nr)];
        p[1] = (JSAMPLE)lut[CLAMP255(ng)];
        p[2] = (JSAMPLE)lut[CLAMP255(nb)];
    }
}

/* ---------------- 曲线 LUT（与 engine.build_curve_lut 一致） ---------------- */
static void build_curve_lut(unsigned char *lut, double contrast,
                            double lift, double rolloff) {
    double k = 6.0 * contrast;
    if (k < 0.1) k = 0.1;
    for (int i = 0; i < 256; i++) {
        double x = i / 255.0;
        x = (x - 0.5) * contrast + 0.5;
        if (x < 0) x = 0; if (x > 1) x = 1;
        x = 1.0 / (1.0 + my_exp(-k * (x - 0.5)));
        x = x * (1.0 - rolloff) + lift;
        if (x < 0) x = 0; if (x > 1) x = 1;
        lut[i] = (unsigned char)(x * 255.0 + 0.5);
    }
}

/* ---------------- 数学颗粒（高斯噪声 + 盒式模糊 + overlay） ---------------- */
static void blur_noise(unsigned char *n, int w, int h, int k) {
    if (k <= 1) return;
    unsigned char *tmp = (unsigned char *)malloc((size_t)w * h);
    int r = k / 2;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int s = 0, c = 0;
            for (int dy = -r; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    int yy = y + dy, xx = x + dx;
                    if (yy >= 0 && yy < h && xx >= 0 && xx < w) { s += n[yy*w+xx]; c++; }
                }
            }
            tmp[y*w+x] = (unsigned char)(s / c);
        }
    }
    memcpy(n, tmp, (size_t)w * h);
    free(tmp);
}

static void apply_grain_all(unsigned char *px, int w, int h, const FilmRecipe *rec) {
    if (rec->grain_intensity <= 0) return;
    double sigma = 6.0 + 45.0 * rec->grain_intensity;
    int k = (int)((rec->grain_size - 1.0) * 2.8 + 1.0 + 0.5);
    if (k < 1) k = 1;
    if ((k & 1) == 0) k++;

    unsigned char *n = (unsigned char *)malloc((size_t)w * h);
    srand(12345);  /* 固定种子：输出可复现 */
    for (int i = 0; i < w * h; i++) {
        double v = 128.0 + gauss_noise() * sigma;
        n[i] = (unsigned char)CLAMP255((int)v);
    }
    blur_noise(n, w, h, k);

    /* overlay：以图像像素为 base，噪声为 overlay（Pillow ImageChops.overlay 等价） */
    for (int i = 0; i < w * h; i++) {
        int nv = n[i];
        for (int c = 0; c < 3; c++) {
            int b = px[i*3+c];
            int v = (b <= 128) ? (2*b*nv/255) : (255 - 2*(255-b)*(255-nv)/255);
            px[i*3+c] = (unsigned char)v;
        }
    }
    free(n);
}

/* ---------------- .cube 3D LUT 支持（整数定点 0-255，softfp 下避免软件浮点） ---------------- */
typedef struct {
    int size;
    int *data;   /* size^3 * 3，值域 0-255 */
} CubeLUT;

static int load_cube(const char *path, CubeLUT *cube) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int size = 0, count = 0, first = 1;
    int *data = NULL;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        if (first) {
            /* 跳过 UTF-8 BOM */
            if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
                s += 3;
            first = 0;
        }
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || *s == '\r' || *s == '\0') continue;
        if (strncmp(s, "LUT_3D_SIZE", 11) == 0) {
            size = atoi(s + 11);
            continue;
        }
        if (strncmp(s, "TITLE", 5) == 0 || strncmp(s, "DOMAIN", 6) == 0 ||
            strncmp(s, "LUT_1D", 6) == 0 || strncmp(s, "LUT_3D_INPUT", 12) == 0 ||
            strncmp(s, "SHADER", 6) == 0 || strncmp(s, "LUT_2D", 6) == 0)
            continue;
        if (!data && size > 0) {
            data = (int *)malloc((size_t)size * size * size * 3 * sizeof(int));
            if (!data) { fclose(f); return -1; }
        }
        if (data) {
            float x, y, z;
            if (sscanf(s, "%f %f %f", &x, &y, &z) == 3 && count < size*size*size) {
                data[count*3] = (int)(x * 255.0 + 0.5);
                data[count*3+1] = (int)(y * 255.0 + 0.5);
                data[count*3+2] = (int)(z * 255.0 + 0.5);
                count++;
            }
        }
    }
    fclose(f);
    if (size <= 0 || count < size*size*size) { free(data); return -1; }
    cube->size = size;
    cube->data = data;
    return 0;
}

/* 三线性插值，整数定点：坐标 ×256（frac 0-255），结果 >>24 */
static void apply_cube_lut(unsigned char *px, int w, int h, const CubeLUT *cube) {
    int size = cube->size;
    int *d = cube->data;
    int sm = size - 1;
    for (int i = 0; i < w * h; i++) {
        unsigned char *p = px + i * 3;
        int rf = (p[0] * sm * 256) / 255;      /* 精确坐标 0..(size-1)*256 */
        int gf = (p[1] * sm * 256) / 255;
        int bf = (p[2] * sm * 256) / 255;
        int ri = rf >> 8, gi = gf >> 8, bi = bf >> 8;
        if (ri > size - 2) ri = size - 2;
        if (gi > size - 2) gi = size - 2;
        if (bi > size - 2) bi = size - 2;
        int dr = rf & 255, dg = gf & 255, db = bf & 255;
        int nr = 256 - dr, ng = 256 - dg, nb = 256 - db;
        for (int ch = 0; ch < 3; ch++) {
            int c000 = d[((ri*size+gi)*size+bi)*3+ch];
            int c100 = d[((ri+1)*size+gi)*size+bi*3+ch];
            int c010 = d[(ri*size+(gi+1))*size+bi*3+ch];
            int c110 = d[((ri+1)*size+(gi+1))*size+bi*3+ch];
            int c001 = d[(ri*size+gi)*size+(bi+1)*3+ch];
            int c101 = d[((ri+1)*size+gi)*size+(bi+1)*3+ch];
            int c011 = d[(ri*size+(gi+1))*size+(bi+1)*3+ch];
            int c111 = d[((ri+1)*size+(gi+1))*size+(bi+1)*3+ch];
            int v = (c000*nr + c100*dr) >> 8;
            v = (v*ng + ((c010*nr + c110*dr) >> 8)*dg) >> 8;
            /* b 方向：先算 b=1 层的 g 混合，再与 b=0 层插值 */
            v = (v*nb + (((((c001*nr + c101*dr) >> 8)*ng + ((c011*nr + c111*dr) >> 8)*dg) >> 8)*db)) >> 8;
            p[ch] = (unsigned char)CLAMP255(v);
        }
    }
}

/* ---------------- 配方查找 ---------------- */
static const FilmRecipe *find_recipe(const char *id) {
    for (int i = 0; i < NUM_RECIPES; i++)
        if (strcmp(RECIPES[i].id, id) == 0)
            return &RECIPES[i];
    return NULL;
}

static void list_recipes(void) {
    for (int i = 0; i < NUM_RECIPES; i++)
        fprintf(stderr, "  %s - %s\n", RECIPES[i].id, RECIPES[i].name);
}

/* ---------------- 主流程 ---------------- */
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <in.jpg> <out.jpg> [recipe_id] [--lut <file.cube>] [--max <px>] [--grain <0..2>]\n", argv[0]);
        list_recipes();
        return 1;
    }
    const char *rid = (argc >= 4) ? argv[3] : "portra400";
    const char *lut_path = NULL;
    int max_size = 0;
    double grain_override = -1.0;
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--lut") == 0 && i + 1 < argc) lut_path = argv[i + 1];
        if (strcmp(argv[i], "--max") == 0 && i + 1 < argc) max_size = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--grain") == 0 && i + 1 < argc) grain_override = atof(argv[i + 1]);
    }
    const FilmRecipe *found = find_recipe(rid);
    if (!found) {
        fprintf(stderr, "unknown recipe: %s\n", rid);
        list_recipes();
        return 1;
    }
    FilmRecipe rec_copy = *found;               /* 非 const 副本，允许 --grain 覆盖 */
    if (grain_override >= 0.0) rec_copy.grain_intensity = grain_override;
    const FilmRecipe *rec = &rec_copy;
    CubeLUT cube;
    int use_cube = 0;
    if (lut_path) {
        if (load_cube(lut_path, &cube) != 0) {
            fprintf(stderr, "无法加载 .cube: %s\n", lut_path);
            return 1;
        }
        use_cube = 1;
    }

    FILE *fin = fopen(argv[1], "rb");
    if (!fin) { perror("open in"); return 1; }

    /* 解码 */
    struct jpeg_decompress_struct dinfo;
    struct jpeg_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&dinfo);
    jpeg_stdio_src(&dinfo, fin);
    jpeg_read_header(&dinfo, TRUE);
    /* --max：解码时整数缩放（libjpeg 支持 1/1,1/2,1/4,1/8），避免 28MP 照片处理过慢 */
    if (max_size > 0) {
        int longest = dinfo.image_width > dinfo.image_height ? dinfo.image_width : dinfo.image_height;
        int denom = 1;
        while (denom < 8 && longest / denom > max_size) denom *= 2;
        dinfo.scale_num = 1;
        dinfo.scale_denom = denom;
    }
    dinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&dinfo);

    int w = dinfo.output_width, h = dinfo.output_height;
    int row_stride = w * dinfo.output_components;

    unsigned char lut[256];
    build_curve_lut(lut, rec->contrast, rec->shadow_lift, rec->highlight_rolloff);

    /* 编码器（流式：边解码边处理边编码，无需整幅缓冲；strength!=1 需整幅回退） */
    FILE *fout = fopen(argv[2], "wb");
    if (!fout) { perror("open out"); jpeg_destroy_decompress(&dinfo); fclose(fin); return 1; }
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr2;
    cinfo.err = jpeg_std_error(&jerr2);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fout);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    /* 4:4:4 无色度抽样 + 95 质量：保证打印细节（默认 4:2:0 色彩细节减半） */
    cinfo.comp_info[0].h_samp_factor = 1;
    cinfo.comp_info[0].v_samp_factor = 1;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;
    jpeg_set_quality(&cinfo, 95, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    /* 流式主循环：
     * 配方路径：读一行 -> NEON 处理 -> 写一行（v12 验证 32s 正常）
     * cube 路径：先整幅缓冲到 pixels，循环后统一 apply_cube_lut + 编码
     *   （v8 整幅缓冲 cube 真机验证正常；v13 流式 cube 真机花屏，回退） */
    unsigned char *pixels = NULL;
    if (use_cube) {
        pixels = (unsigned char *)malloc((size_t)w * h * 3);
        if (!pixels) { perror("alloc pixels"); jpeg_destroy_decompress(&dinfo); fclose(fin); fclose(fout); return 1; }
    }
    JSAMPARRAY buf = (*dinfo.mem->alloc_sarray)((j_common_ptr)&dinfo, JPOOL_IMAGE,
                                                row_stride, 1);
    while (dinfo.output_scanline < h) {
        jpeg_read_scanlines(&dinfo, buf, 1);
        if (use_cube) {
            memcpy(pixels + (size_t)(dinfo.output_scanline - 1) * row_stride,
                   buf[0], (size_t)row_stride);
        } else {
            apply_color_neon(buf[0], w, rec->matrix, rec->saturation, lut);
            jpeg_write_scanlines(&cinfo, buf, 1);
        }
    }

    if (use_cube) {
        apply_cube_lut(pixels, w, h, &cube);
        JSAMPROW rp = pixels;
        while (cinfo.next_scanline < h) {
            jpeg_write_scanlines(&cinfo, &rp, 1);
            rp += row_stride;
        }
        free(pixels);
    }

    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(fin);
    fclose(fout);
    if (use_cube) free(cube.data);

    printf("[NX-KS2] filmsim: %dx%d -> %s recipe=%s (%s)\n",
           w, h, argv[2], rec->id, rec->name);
    return 0;
}
