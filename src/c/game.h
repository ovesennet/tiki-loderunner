/* TIKI LODE RUNNER - game.h
 * The player actor, digging and the hole lifecycle.
 */

#ifndef GAME_H
#define GAME_H

#include "defs.h"

/* Value in Actor.under meaning "this guard has no occupancy marker out". */
#define NOT_MARKED 0xFF

typedef struct {
    uint8_t state;      /* ST_WALKING / ST_FALLING / ST_LADDER / ST_ROPE
                         * and, for guards only, ST_TRAPPED */
    uint8_t x;          /* pixel, always even, 0..248 */
    uint8_t y;          /* pixel, always even, 0..175 */
    int8_t  dir_h;      /* -1 left, 0 none, +1 right */
    int8_t  dir_v;      /* -1 up, 0 none, +1 down */
    uint8_t sprite;     /* glyph currently composited */
    uint8_t frame;      /* animation phase */

    /* Guard-only fields. The original keeps these in the same MAN record
     * for both kinds of actor; the player simply never uses them. */
    uint8_t is_enemy;
    uint8_t has_box;    /* carrying gold (original's GORIGHTLEFT == 1) */
    uint8_t under;      /* terrain remembered beneath an occupancy marker
                         * (original's VAR_10), NOT_MARKED when unmarked */
    uint8_t mark_col;   /* cell the marker was written to */
    uint8_t mark_row;
    uint8_t trap_timer; /* ST_TRAPPED countdown */
    int8_t  turn;       /* last committed horizontal direction, used to
                         * break out of corners (original's GOUPDOWN) */

    /* The footprint this actor was last composited at. Owned solely by the
     * frame compositor, which uses it to erase exactly what it drew. */
    uint8_t px;
    uint8_t py;
    uint8_t pspr;
} Actor;

/* Outcome of one game frame. */
#define PLAY_RUNNING    0
#define PLAY_DIED       1
#define PLAY_COMPLETE   2
#define PLAY_QUIT       3

/* Load a room, paint it and place the player and guards. */
void game_start_room(uint8_t room);

/* Advance one frame: input, player state machine, guards, gold, dig and
 * holes. Returns one of the PLAY_* codes. */
uint8_t game_frame(void);

/* Current player, for the HUD and for debugging. */
extern Actor player;
extern Actor enemies[MAX_ENEMIES];
extern uint8_t enemy_count;
extern uint16_t boxes_left;
extern uint32_t score;

/* Add points and refresh the HUD score field. */
void game_add_score(uint16_t points);

/* Award the completion bonus for the room just finished. */
void game_level_bonus(void);

/* Lives counter, owned by the game module so the HUD stays in step. */
uint8_t game_lives(void);
void game_set_lives(uint8_t lives);

/* Cheat mode: the player cannot be killed. Flagged rather than hidden, the
 * way the original's CHEATED byte marks a run that used its own cheat keys,
 * so a cheated score is never mistaken for an honest one. */
uint8_t game_cheat(void);
void game_set_cheat(uint8_t on);

#endif /* GAME_H */
