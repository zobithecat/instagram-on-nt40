/* core/font.h — tiny 5x7 bitmap font, OS-independent (drawn with core/raster).
 *
 * Printable ASCII 32..126. Glyphs are 5px wide x 7px tall; the advance is 6px
 * (1px gap) and the line height 8px. Rendered pixel-for-pixel (no anti-alias),
 * which suits the crisp NT4 look and stays exactly reproducible in host tests. */
#ifndef CORE_FONT_H
#define CORE_FONT_H

#include "raster.h"

#define FONT_W    5
#define FONT_H    7
#define FONT_ADV  6   /* glyph advance at scale 1 (5px glyph + 1px gap) */
#define FONT_LINE 8   /* line height at scale 1 */

/* Draw NUL-terminated `str` at (x,y) top-left in `color` (uses color's alpha).
 * `scale` >= 1 enlarges each glyph pixel to a scale x scale block. '\n' wraps
 * back to the start x and down one line. Chars outside 32..126 advance blank. */
void font_draw_scaled(Surface *s, int x, int y, const char *str, uint32_t color, int scale);

/* Convenience: scale 1. */
void font_draw(Surface *s, int x, int y, const char *str, uint32_t color);

/* Pixel width the string would occupy at `scale` (no trailing gap). */
int  font_text_width(const char *str, int scale);

#endif /* CORE_FONT_H */
