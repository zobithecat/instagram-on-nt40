"""tools/gen_hangul_font.py -- generates core/font_hangul_data.h, a 14x14 1bpp
bitmap font covering the KS X 1001 (1987 wansung) Hangul syllable block (2350
glyphs -- covers the overwhelming majority of real-world Korean text; this is
the same practical subset classic bitmap Korean fonts targeted, not the full
11,172-syllable modern Unicode block).

Rasterizes each syllable from a real Korean-capable TrueType font (any system
font works; macOS ships AppleGothic.ttf) and thresholds to a monochrome
bitmap, matching core/font.c's existing "crisp, no anti-alias" 5x7 Latin font
aesthetic. Run from the repo root: `python3 tools/gen_hangul_font.py`
(requires Pillow: `pip install pillow`).
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = os.environ.get("HANGUL_SRC_FONT", "/System/Library/Fonts/Supplemental/AppleGothic.ttf")
# 14px looked fine for most syllables but several visually-close vowel pairs
# (e.g. 처/쳐, 저/져) became indistinguishable once thresholded to 1bpp -- the
# distinguishing stroke is only 1-2px at that size and gets lost. 18px keeps
# those readable (checked by rendering known-confusable pairs side by side).
SIZE = 18
THRESHOLD = 110
OUT = os.path.join(os.path.dirname(__file__), "..", "core", "font_hangul_data.h")

def enumerate_ksx1001():
    cps = []
    for lead in range(0xB0, 0xC9):
        for trail in range(0xA1, 0xFF):
            try:
                ch = bytes([lead, trail]).decode('euc-kr')
            except UnicodeDecodeError:
                continue
            cp = ord(ch)
            if 0xAC00 <= cp <= 0xD7A3:
                cps.append(cp)
    assert cps == sorted(cps)
    return cps

def render_bits(ch, size):
    scale = 4
    big = scale * size
    img = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_PATH, int(big * 0.92))
    bbox = draw.textbbox((0, 0), ch, font=font)
    w = bbox[2] - bbox[0]; h = bbox[3] - bbox[1]
    x = (big - w) // 2 - bbox[0]; y = (big - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=255)
    small = img.resize((size, size), Image.LANCZOS)
    px = small.load()
    rows = []
    for row in range(size):
        val = 0
        for col in range(size):
            on = px[col, row] > THRESHOLD
            if on:
                val |= (1 << (size - 1 - col))
        rows.append(val)
    return rows

codepoints = enumerate_ksx1001()
assert len(codepoints) == 2350, len(codepoints)

with open(OUT, "w") as f:
    f.write("/* core/font_hangul_data.h -- GENERATED, do not hand-edit.\n")
    f.write(" * Regenerate with: python3 tools/gen_hangul_font.py\n")
    f.write(" * KS X 1001 (1987 wansung) Hangul syllable block, %d glyphs, %dx%d 1bpp,\n" % (len(codepoints), SIZE, SIZE))
    f.write(" * rasterized from AppleGothic.ttf and thresholded (see tools/gen_hangul_font.py\n")
    f.write(" * for the exact procedure). HANGUL_CODEPOINTS is sorted ascending (binary\n")
    f.write(" * search key); HANGUL_GLYPHS[i] are HANGUL_H row bitmaps for codepoint i,\n")
    f.write(" * MSB-of-the-low-HANGUL_W-bits = leftmost column (same convention as\n")
    f.write(" * core/font.c's FONT[][] table). Row values need up to HANGUL_W=%d bits,\n" % SIZE)
    f.write(" * so HANGUL_GLYPHS is `unsigned int` (a 16-bit type silently truncates\n")
    f.write(" * anything past HANGUL_W=16 -- caught once via -Wconstant-conversion on\n")
    f.write(" * an earlier 18px generation, see git history). */\n")
    f.write("#ifndef CORE_FONT_HANGUL_DATA_H\n#define CORE_FONT_HANGUL_DATA_H\n\n")
    f.write("#define HANGUL_COUNT %d\n" % len(codepoints))
    f.write("#define HANGUL_W %d\n" % SIZE)
    f.write("#define HANGUL_H %d\n\n" % SIZE)

    f.write("static const unsigned short HANGUL_CODEPOINTS[HANGUL_COUNT] = {\n")
    for i in range(0, len(codepoints), 16):
        f.write("    " + ",".join("0x%04x" % c for c in codepoints[i:i+16]) + ",\n")
    f.write("};\n\n")

    f.write("static const unsigned int HANGUL_GLYPHS[HANGUL_COUNT][HANGUL_H] = {\n")
    for idx, cp in enumerate(codepoints):
        rows = render_bits(chr(cp), SIZE)
        f.write("    {" + ",".join("0x%04x" % r for r in rows) + "}, /* U+%04X */\n" % cp)
        if idx % 200 == 0:
            print("progress", idx, "/", len(codepoints))
    f.write("};\n\n#endif /* CORE_FONT_HANGUL_DATA_H */\n")

print("wrote", OUT)
