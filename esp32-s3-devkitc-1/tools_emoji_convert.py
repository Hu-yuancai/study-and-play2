# -*- coding: utf-8 -*-
"""
把 表情包/*.jpg 批量转成 128x64 单色 XBM 位图 C 数组, 供 OLED(U8g2 drawXBM) 显示.
XBM: 每字节8像素, LSB在左(bit0=最左像素), 1=点亮.
输出: src/emoji_bitmaps.h  (PROGMEM 数组 + 分组动画表)
"""
import os, sys
from PIL import Image

SRC = "表情包"
OUT = "include/emoji_bitmaps.h"
W, H = 128, 64

# 用户指定的动画分组 (每组是一个动作的多个分镜)
GROUPS = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9, 10, 11, 18],
    [12, 13, 14],
    [15, 16, 17],
]

def to_xbm_bytes(path):
    img = Image.open(path)
    img.draft("L", (256, 256))                   # 解码时降采样, 避免大图崩溃
    img = img.convert("L")                        # 灰度
    # 等比缩放后居中裁剪到 128x64
    sw, sh = img.size
    scale = max(W / sw, H / sh)
    img = img.resize((max(W,int(sw*scale)), max(H,int(sh*scale))))
    nw, nh = img.size
    left = (nw - W)//2; top = (nh - H)//2
    img = img.crop((left, top, left+W, top+H))
    # 阈值二值化: 暗像素(<128)点亮. 用 getdata 一次性取出, 避免逐像素索引崩溃
    pixels = list(img.getdata())                  # 长度 128*64, 行优先
    data = bytearray()
    for y in range(H):
        rowbase = y * W
        for bx in range(0, W, 8):
            b = 0
            for bit in range(8):
                if pixels[rowbase + bx + bit] < 128:   # 暗处点亮
                    b |= (1 << bit)                     # LSB在左
            data.append(b)
    return data

def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    lines = []
    lines.append("// 自动生成: 表情包 jpg -> 128x64 单色XBM. 勿手改.")
    lines.append("#ifndef EMOJI_BITMAPS_H")
    lines.append("#define EMOJI_BITMAPS_H")
    lines.append("#include <Arduino.h>")
    lines.append("#define EMOJI_W 128")
    lines.append("#define EMOJI_H 64")
    lines.append("")

    all_ids = [n for g in GROUPS for n in g]
    for n in all_ids:
        path = os.path.join(SRC, f"{n}.jpg")
        print("processing", n, flush=True)
        data = to_xbm_bytes(path)
        arr = ",".join(str(b) for b in data)
        lines.append(f"static const uint8_t PROGMEM emoji_{n}[] = {{{arr}}};")
    lines.append("")

    # 分组动画表: 指针数组 + 每组帧数
    lines.append("// 每组动画的帧(指针)")
    for gi, g in enumerate(GROUPS):
        ptrs = ",".join(f"emoji_{n}" for n in g)
        lines.append(f"static const uint8_t* const emojiGroup{gi}[] = {{{ptrs}}};")
    lines.append("")
    lines.append(f"#define EMOJI_GROUP_COUNT {len(GROUPS)}")
    grp_ptrs = ",".join(f"emojiGroup{gi}" for gi in range(len(GROUPS)))
    lines.append(f"static const uint8_t* const* const emojiGroups[] = {{{grp_ptrs}}};")
    frame_counts = ",".join(str(len(g)) for g in GROUPS)
    lines.append(f"static const uint8_t emojiGroupFrames[] = {{{frame_counts}}};")
    lines.append("")
    lines.append("#endif")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("OK ->", OUT, "size(bytes):", os.path.getsize(OUT))

main()
