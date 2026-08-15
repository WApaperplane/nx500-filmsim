#!/bin/bash
# NX-KS2 阶段2A：ARM 交叉编译脚本（Zig，target=arm-linux-gnueabi.2.15）
# 用法: bash build.sh [all|hello|jpeg]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
# Git Bash 的 /d/... 转 Windows 风格 D:/...（zig.exe 是原生 Windows 程序）
HERE_WIN="$(echo "$HERE" | sed 's#^/\([a-z]\)/#\U\1:/#')"
ZIG="D:/download/NX-KS2-88/.uploads/zig/zig-windows-x86_64-0.13.0/zig.exe"
ROOTFS="D:/download/NX-KS2-88/.uploads/nx1_open/rootfs_dev/standard-armv7l/usr"
TARGET="arm-linux-gnueabi.2.15"   # Tizen 2.2 glibc ~2.15, EABI5 softfp
# -O0 关键：-O1/-O2 编译的 ARM 代码在 NX500 真机段错误(139)，-O0 稳定（真机实测 2026-08-15）
# 注意：不要设置 ZIG_LOCAL_CACHE_DIR/GLOBAL_CACHE_DIR 环境变量（沙箱下 AccessDenied），用 zig 默认缓存
CFLAGS="-target $TARGET -O0 -I$ROOTFS/include -mfloat-abi=soft -fno-stack-protector"

mkdir -p "$HERE_WIN/out"

build_hello() {
    echo "== 编译 hello (验证工具链/ABI) =="
    "$ZIG" cc $CFLAGS "$HERE_WIN/hello.c" -o "$HERE_WIN/out/hello.arm"
    echo "  -> out/hello.arm"
}

build_jpeg() {
    echo "== 编译 jpeg_film (链接相机 libjpeg-turbo) =="
    "$ZIG" cc $CFLAGS "$HERE_WIN/jpeg_film.c" -L"$ROOTFS/lib" -ljpeg -o "$HERE_WIN/out/jpeg_film.arm"
    echo "  -> out/jpeg_film.arm"
}

build_jpeg_debug() {
    echo "== 编译 jpeg_film_debug (逐步日志版, -O0) =="
    "$ZIG" cc -target $TARGET -O0 -I$ROOTFS/include -mfloat-abi=soft -fno-stack-protector \
        "$HERE_WIN/jpeg_film_debug.c" -L"$ROOTFS/lib" -ljpeg -o "$HERE_WIN/out/jpeg_film_debug.arm"
    echo "  -> out/jpeg_film_debug.arm"
}

build_filmsim() {
    echo "== 编译 filmsim (完整配方引擎, -O0) =="
    # 源码用 fs2.c（filmsim.c 被沙箱标记读取 AccessDenied，勿改回）
    "$ZIG" cc $CFLAGS -I"$HERE_WIN" "$HERE_WIN/fs2.c" \
        -L"$ROOTFS/lib" -ljpeg -o "$HERE_WIN/out/filmsim.arm"
    echo "  -> out/filmsim.arm"
}

case "${1:-all}" in
    hello)   build_hello ;;
    jpeg)    build_jpeg ;;
    debug)   build_jpeg_debug ;;
    filmsim) build_filmsim ;;
    all)     build_hello; build_jpeg; build_jpeg_debug; build_filmsim ;;
    *) echo "usage: build.sh [all|hello|jpeg|debug|filmsim]"; exit 1 ;;
esac
echo "完成。"
