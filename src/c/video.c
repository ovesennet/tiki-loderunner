/* TIKI LODE RUNNER - video.c
 * Video init and thin C wrappers over the ASM routines in screen.asm and
 * tiles.asm. All pixel work happens in assembly.
 */

#include <arch/tiki100.h>
#include "video.h"

/* screen.asm */
extern void vid_clear_asm(void);
extern void vid_fill_rect_gfx(void);
extern void vid_draw_text_gfx(void);
extern void vsync_init_asm(void);
extern void vsync_shutdown_asm(void);
extern void wait_vsync_asm(void);

extern uint16_t txt_x;
extern uint16_t txt_y;
extern uint16_t txt_str;
extern uint8_t  txt_colour;
extern uint16_t gfx_x1;
extern uint16_t gfx_y1;
extern uint8_t  gfx_colour;
extern uint8_t  gfx_width;
extern uint8_t  gfx_height;

/* tiles.asm */
extern void tile_draw_asm(void);
extern void tile_fill_row_asm(void);

extern uint16_t tile_glyph;
extern uint8_t  tile_col;
extern uint8_t  tile_row;
extern uint8_t  tile_ink;
extern uint8_t  tile_paper;

/* sprite.asm */
extern void sprite_draw_asm(void);

extern uint16_t spr_glyph;
extern uint8_t  spr_x;
extern uint8_t  spr_y;
extern uint8_t  spr_ink;

/* gfxdata.asm - the original 2 KB glyph bank, 256 glyphs of 8 bytes. */
extern uint8_t gfx_bank[];

/* Palette: the ZX Spectrum's 16 attribute colours in Spectrum order
 * (black, blue, red, magenta, green, cyan, yellow, white, then the bright
 * half), so the original SPRITE_COLORS entries map across unchanged.
 * Bytes are plain RRRGGGBB intensities; gr_setpalette complements them for
 * the hardware register, so they must not be complemented here as well. */
static const char palette[16] = {
    /* normal, ~205 intensity */
    0x00, 0x02, 0xC0, 0xC2, 0x18, 0x1A, 0xD8, 0xDA,
    /* bright, full intensity */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF,
};

/* The original SPRITE_COLORS table (Lode_runner_main.asm), carried over
 * verbatim: bright red terrain, bright white ladders and ropes, bright
 * yellow gold. Entries 5 and 6 are only reachable in the editor render
 * context, where hidden objects stay visible in cyan and green. */
static const uint8_t sprite_colours[8] = {
    COL_BRWHITE,    /* 0 empty       */
    COL_BRRED,      /* 1 bricks      */
    COL_BRRED,      /* 2 solid       */
    COL_BRWHITE,    /* 3 ladder      */
    COL_BRWHITE,    /* 4 rope        */
    COL_BRCYAN,     /* 5 hidden brick  (editor view only) */
    COL_BRGREEN,    /* 6 hidden ladder (editor view only) */
    COL_BRYELLOW    /* 7 gold        */
};

uint8_t obj_colour(uint8_t obj)
{
    return sprite_colours[obj & 7];
}

void vid_init(void)
{
    gr_defmod(3);
    gr_setpalette(16, palette);
    vsync_init_asm();
    vid_clear();
}

void vid_shutdown(void)
{
    vsync_shutdown_asm();
    gr_defmod(1);
}

void vid_clear(void) { vid_clear_asm(); }

void vid_fill_rect(uint16_t x, uint16_t y, uint8_t w, uint8_t h, uint8_t colour)
{
    if (w == 0 || h == 0) return;
    gfx_x1 = x; gfx_y1 = y; gfx_width = w; gfx_height = h; gfx_colour = colour;
    vid_fill_rect_gfx();
}

void vid_draw_text(uint16_t x, uint16_t y, const char *str, uint8_t colour)
{
    txt_x = x; txt_y = y; txt_str = (uint16_t)str; txt_colour = colour;
    vid_draw_text_gfx();
}

void wait_vsync(void) { wait_vsync_asm(); }

void cell_draw(uint8_t col, uint8_t row, uint8_t glyph,
               uint8_t ink, uint8_t paper)
{
    tile_glyph = (uint16_t)&gfx_bank[(uint16_t)glyph << 3];
    tile_col   = col;
    tile_row   = row;
    tile_ink   = ink;
    tile_paper = paper;
    tile_draw_asm();
}

void cell_fill_row(uint8_t row, uint8_t colour)
{
    tile_row = row;
    tile_ink = colour;
    tile_fill_row_asm();
}

void sprite_draw(uint8_t x, uint8_t y, uint8_t glyph, uint8_t ink)
{
    spr_glyph = (uint16_t)&gfx_bank[(uint16_t)glyph << 3];
    spr_x     = x;
    spr_y     = y;
    spr_ink   = ink;
    sprite_draw_asm();
}
