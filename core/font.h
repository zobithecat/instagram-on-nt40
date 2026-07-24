/* core/font.h — tiny 5x7 bitmap font, OS-independent (drawn with core/raster).
 *
 * Printable ASCII 32..126. Glyphs are 5px wide x 7px tall; the advance is 6px
 * (1px gap) and the line height 8px. Rendered pixel-for-pixel (no anti-alias),
 * which suits the crisp NT4 look and stays exactly reproducible in host tests.
 *
 * Also renders Hangul: a UTF-8 sequence that decodes to a KS X 1001 (1987
 * wansung) syllable — the ~2350-glyph subset that covers the overwhelming
 * majority of real Korean text, see core/font_hangul_data.h — gets a real
 * 18x18 bitmap glyph (more than 2x FONT_LINE tall!). Any other non-ASCII
 * codepoint (other scripts, emoji, rarer modern Hangul syllables outside
 * that subset) advances blank as one unit per codepoint, same "no garbage,
 * just a gap" philosophy as before.
 *
 * A caller stacking multiple text lines close together (e.g. ui/feed.c's
 * post cards) MUST use FONT_WIDE_LINE, not FONT_LINE, as the vertical pitch
 * for any line that might hold externally-sourced text (a real fetched
 * caption) — FONT_LINE alone will make an 18px-tall Hangul glyph visually
 * overlap the line above/below it. Lines guaranteed to be our own ASCII
 * (usernames — Instagram restricts these to Latin/digits/._ — or our own
 * formatted "N likes" string) can keep the tighter FONT_LINE pitch. */
#ifndef CORE_FONT_H
#define CORE_FONT_H

#include "raster.h"

#define FONT_W    5
#define FONT_H    7
#define FONT_ADV  6   /* glyph advance at scale 1 (5px glyph + 1px gap) */
#define FONT_LINE 8   /* line height at scale 1, ASCII-only lines */

/* Vertical pitch (scale 1) tall enough for the tallest glyph this font can
 * draw (currently the 18px Hangul bitmap, core/font_hangul_data.h's
 * HANGUL_H, +1px gap) -- font.c _Static_assert's this stays in sync. */
#define FONT_WIDE_LINE 19

/* Draw NUL-terminated `str` at (x,y) top-left in `color` (uses color's alpha).
 * `scale` >= 1 enlarges each glyph pixel to a scale x scale block. '\n' wraps
 * back to the start x and down one line. ASCII 32..126 and KS X 1001 Hangul
 * render as real glyphs; any other codepoint advances blank. */
void font_draw_scaled(Surface *s, int x, int y, const char *str, uint32_t color, int scale);

/* Convenience: scale 1. */
void font_draw(Surface *s, int x, int y, const char *str, uint32_t color);

/* Pixel width the string would occupy at `scale` (no trailing gap). Stops at
 * the first '\n', if any -- a caller measuring one line of a multi-line
 * string shouldn't have later lines bleed into the total. */
int  font_text_width(const char *str, int scale);

/* Find the longest byte-length prefix of `str` (scale 1, never splitting a
 * codepoint, stopping at the first '\n' same as font_text_width) whose
 * rendered width doesn't exceed `max_w`. Always includes at least one glyph
 * even if it alone exceeds max_w (so a caller can't get stuck making zero
 * progress while wrapping a long line). Returns the width consumed; the byte
 * length consumed is written to *out_len (may be NULL). For ui/feed.c's
 * caption line-wrapping, so it doesn't need to duplicate this font's UTF-8/
 * Hangul decoding just to measure substrings. */
int  font_fit_width(const char *str, int max_w, int *out_len);

#endif /* CORE_FONT_H */
