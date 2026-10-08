/* TIKI LODE RUNNER - defs.h
 * Shared types and the constants carried over from the Spectrum original.
 */

#ifndef DEFS_H
#define DEFS_H

#include <stdint.h>

/* ── Tiki-100 mode 3: 256x256, 16 colours, 128 bytes/scanline ── */
#define SCREEN_W        256
#define SCREEN_H        256

/* ── Logical playfield, unchanged from the Spectrum ──
 * 32x22 cells of 8x8 pixels = 256x176, then a separator row and the
 * original status line. Gameplay works entirely in this space; only the
 * renderer adds TILE_Y_OFFSET to reach physical scanlines. */
#define ROOM_COLS       32
#define ROOM_ROWS       22
#define ROOM_CELLS      (ROOM_COLS * ROOM_ROWS)     /* 704 */
#define SEP_ROW         22
#define HUD_ROW         23
#define LOGICAL_ROWS    24
#define TILE_Y_OFFSET   32      /* must match TILE_Y_OFFSET in tiles.asm */

/* ── Packed room data ── */
/* Must match the room count held in leveldata.asm, which ships a 50-room
 * subset of the original 75 to keep the image inside the Tiki-100's
 * free RAM. */
#define ROOM_COUNT      50
#define ROOM_TRAIN      0       /* training room, from trainlevel.asm */
#define ROOM_PACKED     264     /* 704 cells x 3 bits */
#define MEN_RECORD      12      /* per-room spawn record */
#define MAX_ENEMIES     5

/* ── OBJECT.* codes (Lode_constants.asm) ── */
#define OBJ_EMPTY       0
#define OBJ_BRICKS      1
#define OBJ_SOLID       2
#define OBJ_LADDER      3
#define OBJ_ROPE        4
#define OBJ_HIDBRICKS   5
#define OBJ_HIDLADDER   6
#define OBJ_BOX         7

/* ── SPRITE.* glyph indices into the original GFX bank ──
 * Terrain glyphs are $20 + OBJECT code; show_cell does the same addition. */
#define GLYPH_TERRAIN   0x20
#define GLYPH_MAN       0x1E
#define GLYPH_BAR       0x05    /* solid bar used for the status row fill */

/* Actor animation glyphs, in the original's bank order. */
#define GLYPH_RIGHT1    0x10    /* running right, 3 frames */
#define GLYPH_LEFT1     0x13    /* running left, 3 frames  */
#define GLYPH_CLIMBER1  0x16    /* on a ladder, 2 frames   */
#define GLYPH_ROPEA1    0x18    /* rope, base used moving right, 3 frames */
#define GLYPH_ROPEB1    0x1B    /* rope, base used otherwise, 3 frames    */

/* Dig and hole-closing animations. The original indexes these dynamically
 * rather than through named constants; each dig phase is two glyphs wide. */
#define GLYPH_DIG_BASE  0xF0    /* phases at +0, +2, +4, +6 */
#define GLYPH_FILL_BASE 0xF8    /* four closing phases      */

/* ── Actor states (the original's ALLOW values) ── */
#define ST_WALKING      0
#define ST_FALLING      1
#define ST_LADDER       2
#define ST_ROPE         3
#define ST_TRAPPED      5       /* original's ALLOW_5 */

/* ── Guard behaviour ──
 * TRAP_TIME is the original's reuse of SPRITE.TRAPPED ($28) as the trapped
 * countdown; it ticks once per enemy turn, and enemies move on alternate
 * frames, so it is about 80 game frames.
 * Guards are caught by, and catch, within five pixels on both axes. */
#define TRAP_TIME       40
#define CATCH_RANGE     6       /* strictly-less-than test, so 0..5 hits */
#define GUARD_PICKUP_MASK 1     /* 1-in-2 chance of taking gold  */
#define GUARD_DROP_MASK   7     /* 1-in-8 chance of dropping it  */

/* ── Dig and hole timing ──
 * A dig runs four phases. Holes tick once every fourth game frame, so a
 * HOLE_LIFECYCLE of 40 is 160 frames, about 3.2 s at 50 Hz. */
#define DIG_PHASES      4
#define HOLES_COUNT     20
#define HOLE_LIFECYCLE  40
#define HOLE_TICK_MASK  3

/* Lateral tolerance when a vertical move is requested, in pixels.
 * Climbing is decided on column boundaries, and the player walks 2 px per
 * step, so without help only one walking position in four lines up with a
 * ladder. Half a cell makes every stopping position work. */
#define CLIMB_SNAP      4

/* Lateral tolerance when a dig is requested, in pixels. The original digs
 * only from an exactly aligned column (do_fire_dig: "and 7 / ret nz"),
 * which on a keyboard means three presses in four are silently swallowed.
 * Snap to the nearest column the same way climbing does. */
#define DIG_SNAP        4

/* ── Points, from the original's score constants. ── */
#define SCORE_BOX       50
#define SCORE_KILL      15
#define SCORE_TRAP      15
#define SCORE_BONUS     10      /* awarded 30 times on level completion */
#define LEVEL_BONUS_STEPS 30

/* Lives the player starts with. */
#define START_LIVES     5

/* ── Render context ──
 * The original switches hidden-object presentation with self-modifying code
 * at cell_smc_remap. Expressed here as an explicit flag instead.
 *   RENDER_GAME   hidden bricks look like bricks, hidden ladders look empty
 *   RENDER_EDITOR hidden objects are drawn with their own distinct glyphs */
#define RENDER_GAME     0
#define RENDER_EDITOR   1

/* ── Palette indices ──
 * The palette mirrors the ZX Spectrum attribute colours so the original
 * SPRITE_COLORS values carry over unchanged: index = colour | BRIGHT. */
#define COL_BLACK       0
#define COL_BLUE        1
#define COL_RED         2
#define COL_MAGENTA     3
#define COL_GREEN       4
#define COL_CYAN        5
#define COL_YELLOW      6
#define COL_WHITE       7
#define COL_BRIGHT      8
#define COL_BRBLUE      (COL_BRIGHT | COL_BLUE)
#define COL_BRRED       (COL_BRIGHT | COL_RED)
#define COL_BRMAGENTA   (COL_BRIGHT | COL_MAGENTA)
#define COL_BRGREEN     (COL_BRIGHT | COL_GREEN)
#define COL_BRCYAN      (COL_BRIGHT | COL_CYAN)
#define COL_BRYELLOW    (COL_BRIGHT | COL_YELLOW)
#define COL_BRWHITE     (COL_BRIGHT | COL_WHITE)

/* ── Spawn record decoded from LEVELS ──
 * Byte 0 is the player row, byte 1 is (player column << 3) | enemy count,
 * then up to five (row, column) enemy pairs. The original variable names
 * PLAYER_COL / PLAYER_ROW are swapped relative to what they hold; the byte
 * encoding below is what the code actually uses. */
typedef struct {
    uint8_t player_row;
    uint8_t player_col;
    uint8_t enemy_count;
    uint8_t enemy_row[MAX_ENEMIES];
    uint8_t enemy_col[MAX_ENEMIES];
} Spawn;

#endif /* DEFS_H */
