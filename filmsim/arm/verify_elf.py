# -*- coding: utf-8 -*-
"""验证交叉编译产物的 ELF 属性（ARM/EABI5/softfp/动态依赖）。"""
import struct
import sys


def parse(path):
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:4] != b"\x7fELF":
        return "非 ELF 文件"
    e_machine = struct.unpack("<H", d[18:20])[0]
    e_flags = struct.unpack("<I", d[36:40])[0]
    eabi = (e_flags >> 24) & 0xf
    e_type = struct.unpack("<H", d[16:18])[0]
    type_name = {1: "REL(可重定位)", 2: "EXEC(可执行)", 3: "DYN(动态)"}.get(e_type, hex(e_type))
    machine_name = {40: "ARM", 183: "AArch64", 62: "x86-64", 3: "i386"}.get(e_machine, hex(e_machine))
    # 动态依赖（粗略字符串扫描）
    deps = []
    for s in (b"libc.so.6", b"libjpeg.so.8", b"libtint-util", b"libmm-type", b"libgcc_s"):
        if s in d:
            deps.append(s.decode())
    return (f"machine={e_machine}({machine_name}) | eabi_v{eabi} | flags=0x{e_flags:08x} "
            f"| type={type_name} | 动态依赖: {deps or '无'}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(f"{p}:")
        print(f"  {parse(p)}")
