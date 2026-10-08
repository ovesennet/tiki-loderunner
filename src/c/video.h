/* TIKI LODE RUNNER - video.h
 * Mode 3 setup plus the cell renderer the game draws through.
 */

#ifndef VIDEO_H
#define VIDEO_H

#include "defs.h"

void vid_init(void);
void vid_shutdown(void);
void vid_clear(void);
void vid_fill_rect(uint16_t x, uint16_t y, uint8_t w, uint8_t h, uint8_t colour);
void vid_draw_text(uint16_t x, uint16_t y, const char *str, uint8_t colour);

/* Wait for the next frame tick (~50 Hz cursor-blink callback). */
void wait_vsync(void);

/* ── Cell renderer ──
 * Draws one 8x8 original glyph at logical cell (col, row). The renderer is
 * the only place that knows about TILE_Y_OFFSET. */
void cell_draw(uint8_t col, uint8_t row, uint8_t glyph,
               uint8_t ink, uint8_t paper);

/* Fill all 32 cells of a logical row with a flat colour. */
void cell_fill_row(uint8_t row, uint8_t colour);

/* ── Actor compositor ──
 * Overlay one 8x8 glyph at a free pixel position. Set bits are painted in
 * `ink`; clear bits are transparent, so the terrain already painted at that
 * position shows through. X must be even, which actor movement guarantees.
 * Callers repaint the covered terrain cells first (invalidation), then
 * overlay; there is deliberately no erase pass and no XOR. */
void sprite_draw(uint8_t x, uint8_t y, uint8_t glyph, uint8_t ink);

/* Palette index the original SPRITE_COLORS table assigns to an object code. */
uint8_t obj_colour(uint8_t obj);

#endif /* VIDEO_H */
