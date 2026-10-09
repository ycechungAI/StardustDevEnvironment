"""Generates 3rdparty/openbw/openbw/ui/hud_font.h: printable ASCII from DejaVu Sans Mono Bold, rasterized once into
anti-aliased 4-bit glyphs for the live game window's HUD (OpenBW has no text rendering of its own).

Usage: python3 tools/gen_hud_font.py [path/to/DejaVuSansMono-Bold.ttf] [pixel size]
Needs Pillow (not one of the project's dependencies: the header is checked in, this only regenerates it).
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "3rdparty" / "openbw" / "openbw" / "ui" / "hud_font.h"
DEFAULT_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

LICENSE = """\
// Glyphs rasterized from DejaVu Sans Mono Bold by tools/gen_hud_font.py - do not edit.
//
// Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is a trademark of Bitstream, Inc.
// DejaVu changes are in public domain.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of the fonts accompanying this
// license ("Fonts") and associated documentation files (the "Font Software"), to reproduce and distribute the Font
// Software, including without limitation the rights to use, copy, merge, publish, distribute, and/or sell copies of
// the Font Software, and to permit persons to whom the Font Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright and trademark notices and this permission notice shall be included in all copies of one or
// more of the Font Software typefaces.
//
// The Font Software may be modified, altered, or added to, and in particular the designs of glyphs or characters in
// the Fonts may be modified and additional glyphs or characters may be added to the Fonts, only if the fonts are
// renamed to names not containing either the words "Bitstream" or the word "Vera".
//
// This License becomes null and void to the extent applicable to Fonts or Font Software that has been modified and
// is distributed under the "Bitstream Vera" names.
//
// The Font Software may be sold as part of a larger software package but no copy of one or more of the Font
// Software typefaces may be sold by itself.
//
// THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
// LIMITED TO ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT,
// PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME FOUNDATION BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF THE USE OR INABILITY TO USE THE FONT
// SOFTWARE OR FROM OTHER DEALINGS IN THE FONT SOFTWARE.
"""


def main() -> int:
    font_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_FONT
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    font = ImageFont.truetype(font_path, size)
    ascent, descent = font.getmetrics()
    width = round(font.getlength("M"))
    height = ascent + descent

    glyphs = []
    for code in range(32, 127):
        image = Image.new("L", (width, height), 0)
        ImageDraw.Draw(image).text((0, 0), chr(code), font=font, fill=255)
        # 4 bits of coverage per pixel, two pixels per byte
        levels = [(value * 15 + 127) // 255 for value in image.tobytes()]
        if len(levels) % 2:
            levels.append(0)
        glyphs.append(bytes((levels[i] << 4) | levels[i + 1] for i in range(0, len(levels), 2)))

    bytes_per_glyph = len(glyphs[0])
    lines = [LICENSE, "#pragma once", "", "#include <cstdint>", "", "namespace hud_font {", "",
             f"constexpr int width = {width};", f"constexpr int height = {height};",
             "constexpr int first_char = 32;", "constexpr int last_char = 126;",
             f"constexpr int bytes_per_glyph = {bytes_per_glyph};", "",
             "// Coverage of each pixel, row by row: 4 bits each, high nibble first",
             "constexpr uint8_t glyphs[last_char - first_char + 1][bytes_per_glyph] = {"]
    for code, glyph in zip(range(32, 127), glyphs):
        body = ",".join(f"0x{b:02x}" for b in glyph)
        lines.append(f"    {{{body}}}, // {chr(code)!r}")
    lines += ["};", "", "}", ""]
    OUTPUT.write_text("\n".join(lines))
    print(f"{OUTPUT.relative_to(ROOT)}: {width}x{height} glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
