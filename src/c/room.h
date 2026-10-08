/* TIKI LODE RUNNER - room.h
 * Room decoding and static presentation.
 */

#ifndef ROOM_H
#define ROOM_H

#include "defs.h"

/* Live collision/AI grid, 22 rows x 32 cols of OBJECT codes. This keeps the
 * original UNPACKED_ROOM semantics, including the temporary occupancy
 * markers the enemy code writes into it. */
extern uint8_t unpacked_room[ROOM_CELLS];

/* Visible terrain glyph per logical cell, for the whole 32x24 image. The
 * compositor repaints from this, never from unpacked_room. */
extern uint8_t visual_tile[ROOM_COLS * LOGICAL_ROWS];
extern uint8_t visual_attr[ROOM_COLS * LOGICAL_ROWS];

/* Decode one packed room (1-based) into unpacked_room. */
void room_unpack(uint8_t room);

/* Decode one room's spawn record (1-based). */
void room_spawn(uint8_t room, Spawn *out);

/* Build visual_tile/visual_attr from unpacked_room for the given render
 * context (RENDER_GAME or RENDER_EDITOR) and paint the whole playfield. */
void room_present(uint8_t context);

/* Separator row plus the original status line. */
void hud_draw(uint8_t room, uint8_t lives, const char *score);

/* Repaint one logical cell from the visual layer, discarding whatever an
 * actor had composited over it. This is the invalidation half of the
 * dirty-cell compositor. */
void room_repaint_cell(uint8_t col, uint8_t row);

/* Object code currently in the collision grid, or SOLID outside the room so
 * callers can treat the border as a wall without bounds-checking. */
uint8_t room_obj(uint8_t col, uint8_t row);

/* Change a cell's terrain: updates the collision grid, rebuilds its visual
 * glyph/colour for the render context room_present last used, and repaints
 * it. Used by digging, hole refill, gold pickup and transform_ladders. */
void room_set_cell(uint8_t col, uint8_t row, uint8_t obj);

/* Change only the collision grid, leaving the visible tile alone. Guards
 * use this for their temporary SOLID occupancy markers. */
void room_set_logical(uint8_t col, uint8_t row, uint8_t obj);

/* Update just the HUD score or lives field in place. */
void hud_set_score(uint32_t value);
void hud_set_lives(uint8_t lives);

/* Flag a cheated run in the separator bar. */
void hud_set_cheat(uint8_t on);

/* Reveal every hidden ladder once the last gold is taken. */
void room_transform_ladders(void);

/* Number of BOX cells in the current room. */
uint16_t room_count_boxes(void);

/* Draw the player and guards at their spawn cells. */
void spawn_draw(const Spawn *sp);

#endif /* ROOM_H */
