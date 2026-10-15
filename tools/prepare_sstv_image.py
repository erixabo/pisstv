#!/usr/bin/env python3
"""Prepare a sourced image as a framed SSTV RGB raster and preview PNG."""
# Copyright (C) 2026 erixabo
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageOps, ImageFont

SIZES = {
    'M1': (320,256), 'M2': (320,256), 'S1': (320,256), 'S2': (320,256), 'DX': (320,256),
    'SC2-60': (320,256), 'SC2-120': (320,256), 'SC2-180': (320,256),
    'ROBOT8': (160,120), 'ROBOT24': (160,120), 'BW12': (160,120),
    'ROBOT36': (320,240), 'ROBOT72': (320,240),
    'PD50': (320,256), 'PD90': (320,256), 'PD120': (640,496), 'PD160': (512,400),
    'PD180': (640,496), 'PD240': (640,496), 'PD290': (800,616),
    'P3': (640,496), 'P5': (640,496), 'P7': (640,496),
    'MP73': (320,256), 'MP115': (320,256), 'MP140': (320,256), 'MP175': (320,256),
    'MR73': (320,256), 'MR90': (320,256), 'MR115': (320,256), 'MR140': (320,256), 'MR175': (320,256),
    'ML180': (640,496), 'ML240': (640,496), 'ML280': (640,496), 'ML320': (640,496),
    'FAX480': (512,500),
}

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--mode', choices=SIZES, default='PD180')
    p.add_argument('--title', default='')
    p.add_argument('--out', type=Path, help='Output prefix; defaults beside source')
    args = p.parse_args()
    w, h = SIZES[args.mode]
    prefix = args.out or args.source.with_name(args.source.stem + '_' + args.mode.lower())
    with Image.open(args.source) as im:
        src = ImageOps.exif_transpose(im).convert('RGB')
    # Use a subdued, blurred full-frame background, then fit the complete source
    # image inside a clean inset so portrait faces and square artwork stay intact.
    canvas = ImageOps.fit(src, (w, h), method=Image.Resampling.LANCZOS)
    canvas = ImageEnhance.Brightness(canvas.filter(ImageFilter.GaussianBlur(max(3, w // 50)))).enhance(0.32)
    draw = ImageDraw.Draw(canvas, 'RGBA')
    pad = max(5, w // 40)
    title_h = max(18, h // 9)
    box = (pad, pad, w-pad, h-title_h-pad)
    inner_w, inner_h = box[2]-box[0], box[3]-box[1]
    fitted = ImageOps.contain(src, (inner_w, inner_h), method=Image.Resampling.LANCZOS)
    x = box[0] + (inner_w-fitted.width)//2
    y = box[1] + (inner_h-fitted.height)//2
    canvas.paste(fitted, (x, y))
    draw.rectangle((x-1, y-1, x+fitted.width, y+fitted.height), outline=(255,255,255,230), width=max(1,w//320))
    draw.rectangle((0, h-title_h, w, h), fill=(0,0,0,205))
    font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
    try:
        font = ImageFont.truetype(font_path, max(9, h//22))
    except OSError:
        font = ImageFont.load_default()
    label = args.title or args.source.stem.replace('_',' ').upper()
    draw.text((pad, h-title_h+(title_h-font.size)//2), label[:72], font=font, fill=(255,255,255,255))
    canvas = canvas.convert('RGB')
    png = prefix.with_suffix('.png')
    raw = prefix.with_suffix('.rgb')
    png.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(png, optimize=True)
    raw.write_bytes(canvas.tobytes())
    print(f'{args.mode}: {w}x{h}; {png}; {raw} ({raw.stat().st_size} bytes)')

if __name__ == '__main__':
    main()
