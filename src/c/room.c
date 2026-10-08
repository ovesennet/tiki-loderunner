/* TIKI LODE RUNNER - room.c
 * Packed-room decoding and static presentation.
 *
 * Three representations are kept apart:
 *   unpacked_room  collision/AI grid (may hold temporary occupancy markers)
 *   visual_tile    the glyph actually painted in each logical cell
 *   visual_attr    that cell's colour, independent of any actor
 * Terrain is never repainted from unpacked_room.
 */

#include "room.h"
#include "video.h"

/* leveldata.asm */
extern uint8_t level_data[];    /* ROOM_COUNT x 264 packed bytes */
extern uint8_t level_men[];     /* ROOM_COUNT x 12 spawn bytes   */

/* trainlevel.asm: room 0, the training level. Not one of the shipped
 * rooms, so it carries its own records rather than extending level_data. */
extern uint8_t train_data[];
extern uint8_t train_men[];

uint8_t unpacked_room[ROOM_CELLS];
uint8_t visual_tile[ROOM_COLS * LOGICAL_ROWS];
uint8_t visual_attr[ROOM_COLS * LOGICAL_ROWS];

/* The context room_present last built the visual layer with, so later
 * terrain edits present hidden objects the same way. */
static uint8_t s_context = RENDER_GAME;

/* ------------------------------------------------------------------
 * Decode a packed room. The 704 cells are a big-endian 3-bit stream over
 * the 264 bytes; the original achieves the same by repeatedly taking the
 * top three bits and shifting the whole buffer left.
 * ------------------------------------------------------------------ */
void room_unpack(uint8_t room)
{
    uint8_t *p;
    uint16_t i, bitpos, byteidx, w;
    uint8_t sh;

    p = &level_data[(uint16_t)(room - 1) * ROOM_PACKED];
    if (room == ROOM_TRAIN)
        p = train_data;
    bitpos = 0;
    for (i = 0; i < ROOM_CELLS; i++) {
        byteidx = bitpos >> 3;
        sh = (uint8_t)(bitpos & 7);
        w = (uint16_t)p[byteidx] << 8;
        if (byteidx + 1 < ROOM_PACKED)
            w |= (uint16_t)p[byteidx + 1];
        unpacked_room[i] = (uint8_t)((w >> (13 - sh)) & 7);
        bitpos += 3;
    }
}

/* ------------------------------------------------------------------
 * Spawn record: byte 0 = player row, byte 1 = (player col << 3) | enemies,
 * then up to five (row, col) enemy pairs. $01 marks an unused slot, but the
 * enemy count already bounds the loop, so the markers are simply ignored.
 * ------------------------------------------------------------------ */
void room_spawn(uint8_t room, Spawn *out)
{
    uint8_t *p;
    uint8_t i;

    p = &level_men[(uint16_t)(room - 1) * MEN_RECORD];
    if (room == ROOM_TRAIN)
        p = train_men;
    out->player_row  = p[0] & 0x1F;
    out->player_col  = p[1] >> 3;
    out->enemy_count = p[1] & 0x07;
    if (out->enemy_count > MAX_ENEMIES)
        out->enemy_count = MAX_ENEMIES;
    for (i = 0; i < MAX_ENEMIES; i++) {
        out->enemy_row[i] = p[2 + i * 2];
        out->enemy_col[i] = p[3 + i * 2];
    }
}

/* ------------------------------------------------------------------
 * Build the visual layer and paint it.
 *
 * In the gameplay context the original patches cell_smc_remap to a NOP so
 * hidden bricks are drawn as ordinary bricks and hidden ladders as empty
 * space. The editor context leaves them visible with their own glyphs.
 * ------------------------------------------------------------------ */
void room_present(uint8_t context)
{
    uint16_t idx;
    uint8_t row, col, obj;

    s_context = context;
    idx = 0;
    for (row = 0; row < ROOM_ROWS; row++) {
        for (col = 0; col < ROOM_COLS; col++) {
            obj = unpacked_room[idx];
            if (context == RENDER_GAME) {
                if (obj == OBJ_HIDBRICKS) obj = OBJ_BRICKS;
                else if (obj == OBJ_HIDLADDER) obj = OBJ_EMPTY;
            }
            visual_tile[idx] = (uint8_t)(GLYPH_TERRAIN + obj);
            visual_attr[idx] = obj_colour(obj);
            cell_draw(col, row, visual_tile[idx], visual_attr[idx], COL_BLACK);
            idx++;
        }
    }
}

/* ------------------------------------------------------------------
 * Status presentation. Row 22 is the original separator (a solid bar in
 * magenta), row 23 the "SCORE / LIVES / LEVEL" line. Column positions match
 * show_infoline exactly.
 * ------------------------------------------------------------------ */
static void hud_text(uint8_t col, const char *s, uint8_t colour)
{
    while (*s && col < ROOM_COLS) {
        cell_draw(col, HUD_ROW, (uint8_t)*s, colour, COL_BLACK);
        col++;
        s++;
    }
}

static void hud_number(uint8_t col, uint16_t value, uint8_t digits,
                       uint8_t colour)
{
    uint8_t i;
    for (i = digits; i != 0; i--) {
        cell_draw((uint8_t)(col + i - 1), HUD_ROW,
                  (uint8_t)('0' + (value % 10)), colour, COL_BLACK);
        value /= 10;
    }
}

void hud_draw(uint8_t room, uint8_t lives, const char *score)
{
    uint8_t col;

    /* Separator row: glyph 5 (solid bar) across all 32 cells, magenta. */
    for (col = 0; col < ROOM_COLS; col++)
        cell_draw(col, SEP_ROW, GLYPH_BAR, COL_MAGENTA, COL_BLACK);

    hud_text(0, "SCORE         LIVES     LEVEL   ", COL_CYAN);
    hud_text(6, score, COL_BRYELLOW);
    hud_number(20, lives, 3, COL_BRYELLOW);
    hud_number(30, room, 2, COL_BRYELLOW);
}

void hud_set_score(uint32_t value)
{
    uint8_t i;
    /* Seven digits, the width the original reserves. */
    for (i = 7; i != 0; i--) {
        cell_draw((uint8_t)(6 + i - 1), HUD_ROW,
                  (uint8_t)('0' + (uint8_t)(value % 10)),
                  COL_BRYELLOW, COL_BLACK);
        value /= 10;
    }
}

void hud_set_lives(uint8_t lives)
{
    hud_number(20, lives, 3, COL_BRYELLOW);
}

/* A cheated run is marked in the separator bar rather than hidden, following
 * the original's CHEATED byte: the score still counts up, but it is never
 * passed off as an honest one. */
void hud_set_cheat(uint8_t on)
{
    const char *s;
    uint8_t col;
    uint8_t colour;

    colour = COL_MAGENTA;
    if (on != 0)
        colour = COL_BRRED;

    for (col = 0; col < ROOM_COLS; col++)
        cell_draw(col, SEP_ROW, GLYPH_BAR, colour, COL_BLACK);

    if (on == 0)
        return;

    s = "CHEAT";
    col = 13;
    while (*s) {
        cell_draw(col, SEP_ROW, (uint8_t)*s, COL_BRWHITE, COL_BLACK);
        col++;
        s++;
    }
}

void spawn_draw(const Spawn *sp)
{
    uint8_t i;

    /* SPRITE_COLORS entries 8 and 9: white player, bright magenta guards. */
    cell_draw(sp->player_col, sp->player_row, GLYPH_MAN, COL_WHITE, COL_BLACK);
    for (i = 0; i < sp->enemy_count; i++)
        cell_draw(sp->enemy_col[i], sp->enemy_row[i], GLYPH_MAN,
                  COL_BRMAGENTA, COL_BLACK);
}

/* ------------------------------------------------------------------
 * Live terrain access and mutation.
 * ------------------------------------------------------------------ */
void room_repaint_cell(uint8_t col, uint8_t row)
{
    uint16_t idx;

    if (col >= ROOM_COLS || row >= LOGICAL_ROWS)
        return;
    idx = (uint16_t)row * ROOM_COLS + col;
    cell_draw(col, row, visual_tile[idx], visual_attr[idx], COL_BLACK);
}

uint8_t room_obj(uint8_t col, uint8_t row)
{
    /* Outside the playfield reads as SOLID so movement code can probe
     * freely without guarding every lookup. */
    if (col >= ROOM_COLS || row >= ROOM_ROWS)
        return OBJ_SOLID;
    return unpacked_room[(uint16_t)row * ROOM_COLS + col];
}

void room_set_cell(uint8_t col, uint8_t row, uint8_t obj)
{
    uint16_t idx;
    uint8_t vis;

    if (col >= ROOM_COLS || row >= ROOM_ROWS)
        return;

    idx = (uint16_t)row * ROOM_COLS + col;
    unpacked_room[idx] = obj;

    vis = obj;
    if (s_context == RENDER_GAME) {
        if (vis == OBJ_HIDBRICKS) vis = OBJ_BRICKS;
        else if (vis == OBJ_HIDLADDER) vis = OBJ_EMPTY;
    }
    visual_tile[idx] = (uint8_t)(GLYPH_TERRAIN + vis);
    visual_attr[idx] = obj_colour(vis);
    cell_draw(col, row, visual_tile[idx], visual_attr[idx], COL_BLACK);
}

void room_set_logical(uint8_t col, uint8_t row, uint8_t obj)
{
    /* Collision grid only. The guard occupancy markers go through here so
     * they can never reach the visual layer, which is what stops a guard
     * standing on empty floor from painting a solid block. */
    if (col >= ROOM_COLS || row >= ROOM_ROWS)
        return;
    unpacked_room[(uint16_t)row * ROOM_COLS + col] = obj;
}

void room_transform_ladders(void)
{
    uint8_t row, col;

    for (row = 0; row < ROOM_ROWS; row++)
        for (col = 0; col < ROOM_COLS; col++)
            if (unpacked_room[(uint16_t)row * ROOM_COLS + col] == OBJ_HIDLADDER)
                room_set_cell(col, row, OBJ_LADDER);
}

uint16_t room_count_boxes(void)
{
    uint16_t i, n = 0;

    for (i = 0; i < ROOM_CELLS; i++)
        if (unpacked_room[i] == OBJ_BOX)
            n++;
    return n;
}
