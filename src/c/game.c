/* TIKI LODE RUNNER - game.c
 * The player state machine, input adapters, digging, the hole lifecycle
 * and the guard layer.
 *
 * Movement works on the logical 32x22 playfield in 2-pixel steps, exactly
 * as the Spectrum original does. Because every actor starts cell-aligned
 * and only ever moves by 2, X stays even, which is what lets the sprite
 * compositor skip nibble shifting entirely.
 *
 * Drawing uses invalidation rather than the original's XOR: the cells the
 * actor covered are repainted from the visual layer and the actor is then
 * composited on top with transparent background pixels. Nothing is ever
 * un-drawn, so no XOR residue can survive a state change.
 */

#include "game.h"
#include "video.h"
#include "input.h"
#include "room.h"
#include "sound.h"

Actor player;
uint16_t boxes_left;
uint32_t score;

static uint8_t  s_room;
static uint16_t s_timer;
static uint8_t  s_lives = START_LIVES;
static uint8_t  s_cheat;

/* Dig animation. s_dig_phase is DIG_IDLE when nothing is being dug. */
#define DIG_IDLE 0xFF
static uint8_t s_dig_phase;
static uint8_t s_dig_col;       /* target cell: the gap above the brick */
static uint8_t s_dig_row;
static int8_t  s_dig_dir = 1;   /* persists between presses, as in the original */

/* Holes. life is HOLE_FREE in an unused slot. */
#define HOLE_FREE 0xFF
static uint8_t s_hole_life[HOLES_COUNT];
static uint8_t s_hole_col[HOLES_COUNT];
static uint8_t s_hole_row[HOLES_COUNT];

/* ==================================================================
 * Terrain predicates
 * ================================================================== */

/* Blocks movement. Hidden bricks behave as ordinary bricks during play. */
static uint8_t solid_move(uint8_t o)
{
    /* Deliberately not written as `(a || b || c) ? 1 : 0`: sccz80 compiles the
     * conditional operator over a logical chain by re-testing the carry flag
     * left over from the last comparison, which inverts the result. */
    if (o == OBJ_BRICKS)    return 1;
    if (o == OBJ_SOLID)     return 1;
    if (o == OBJ_HIDBRICKS) return 1;
    return 0;
}

/* Holds a walking actor up. A ladder supports from below; a rope does not. */
static uint8_t supports(uint8_t o)
{
    if (o == OBJ_BRICKS) return 1;
    if (o == OBJ_SOLID)  return 1;
    if (o == OBJ_LADDER) return 1;
    return 0;
}

static uint8_t obj_px(uint8_t x, uint8_t y)
{
    return room_obj((uint8_t)(x >> 3), (uint8_t)(y >> 3));
}

/* The original's get_obj_horz: an actor whose X is not cell-aligned
 * straddles two columns, so both must be considered. The second cell reads
 * as EMPTY when the actor is aligned. */
static uint8_t obj_px2(uint8_t x, uint8_t y)
{
    if ((x & 7) == 0)
        return OBJ_EMPTY;
    return room_obj((uint8_t)((x >> 3) + 1), (uint8_t)(y >> 3));
}

static uint8_t blocked_h(uint8_t x, uint8_t y)
{
    /* Written as separate statements: sccz80 miscompiles `||` applied to two
     * nested function calls, losing the first call's result. */
    if (solid_move(obj_px(x, y)))
        return 1;
    if (solid_move(obj_px2(x, y)))
        return 1;
    return 0;
}

static uint8_t supported_at(uint8_t x, uint8_t y)
{
    if (supports(obj_px(x, y)))
        return 1;
    if (supports(obj_px2(x, y)))
        return 1;
    return 0;
}

/* Top scanline of the cell below pixel row y. */
static uint8_t row_below(uint8_t y)
{
    return (uint8_t)(((y >> 3) + 1) << 3);
}

/* ==================================================================
 * Compositor
 * ================================================================== */

/* Repaint the up-to-four terrain cells an 8x8 sprite at (x,y) covers. */
static void invalidate(uint8_t x, uint8_t y)
{
    uint8_t col = (uint8_t)(x >> 3);
    uint8_t row = (uint8_t)(y >> 3);

    room_repaint_cell(col, row);
    if (x & 7) room_repaint_cell((uint8_t)(col + 1), row);
    if (y & 7) {
        room_repaint_cell(col, (uint8_t)(row + 1));
        if (x & 7) room_repaint_cell((uint8_t)(col + 1), (uint8_t)(row + 1));
    }
}

static void actor_draw(const Actor *a)
{
    /* SPRITE_COLORS entries 8 and 9: white player, bright magenta guards. */
    sprite_draw(a->x, a->y, a->sprite,
                a->is_enemy ? COL_BRMAGENTA : COL_WHITE);
}

/* Erase an actor's previous footprint and draw its current one.
 *
 * Sprites are transparent overlays, so the cells an actor covered must be
 * repainted before it is drawn again. Every erase in the game funnels
 * through here, immediately before the matching draw, so an actor is never
 * missing from the screen for longer than one repaint. Erases scattered
 * through the movement code were what made the player flicker: it was
 * rubbed out at the top of the frame and only redrawn at the bottom, and
 * with no double buffer the raster caught the gap.
 *
 * The erase is skipped entirely when neither the footprint nor the glyph
 * moved, because repainting an identical glyph over itself is invisible
 * while erasing it first is exactly what makes a standing actor blink.
 * The draw is unconditional, so terrain repainted underneath an actor
 * during the frame (a closing hole, collected gold) cannot leave it
 * half-erased. */
static void actor_composite(Actor *a)
{
    if (a->px != a->x || a->py != a->y || a->pspr != a->sprite)
        invalidate(a->px, a->py);

    actor_draw(a);

    a->px   = a->x;
    a->py   = a->y;
    a->pspr = a->sprite;
}

/* Adopt the current position as the composited one, for an actor that has
 * just been placed rather than moved. */
static void actor_place(Actor *a)
{
    a->px   = a->x;
    a->py   = a->y;
    a->pspr = a->sprite;
}

/* ==================================================================
 * Player state handlers
 *
 * Each handler reads the requested direction and updates the actor in
 * place. They never draw; the caller invalidates the old footprint and
 * composites the new one once per frame.
 * ================================================================== */

static void state_walking(Actor *a, int8_t in_h, int8_t in_v)
{
    uint8_t below_y, cur, nx, off, cx, may_v;

    /* Vertical moves are judged against a column boundary, which is what
     * keeps an actor from entering a ladder half a cell across. Demanding
     * an exact hit is far too exacting on a keyboard, though: the player
     * walks 2 px at a time, so only one stopping position in four would
     * line up. Pick the nearest boundary within CLIMB_SNAP instead, with
     * ties going the way the player is already walking, and judge that
     * column. Guards get no tolerance -- their AI aligns itself, and their
     * occupancy markers are only written on exact cell boundaries. */
    off   = (uint8_t)(a->x & 7);
    cx    = a->x;
    may_v = 0;
    if (off == 0)
        may_v = 1;
    if (off != 0 && !a->is_enemy) {
        cx = (uint8_t)(a->x - off);
        if (off > CLIMB_SNAP)
            cx = (uint8_t)(cx + 8);
        if (off == CLIMB_SNAP && a->dir_h > 0)
            cx = (uint8_t)(cx + 8);
        may_v = 1;
    }

    /* The snap is only committed if that column really does afford the
     * move, so the player is never dragged sideways for nothing. */
    if (may_v) {
        if (in_v < 0) {
            /* Climbing up requires a ladder underfoot and headroom. */
            if (obj_px(cx, a->y) == OBJ_LADDER && a->y >= 8 &&
                !solid_move(obj_px(cx, (uint8_t)(a->y - 8)))) {
                a->x      = cx;
                a->state  = ST_LADDER;
                a->dir_v  = -1;
                a->dir_h  = 0;
                a->sprite = GLYPH_CLIMBER1;
                return;
            }
        }

        if (in_v > 0) {
            /* Stepping down is permissive: the original also accepts empty
             * space, gold and hidden ladders here, not just a real ladder. */
            below_y = row_below(a->y);
            if (below_y < (ROOM_ROWS * 8)) {
                cur = obj_px(cx, below_y);
                if (cur == OBJ_EMPTY || cur == OBJ_BOX ||
                    cur == OBJ_HIDLADDER || cur == OBJ_LADDER) {
                    /* The cell *below* cx being open says nothing about cx
                     * itself. Snapping sideways into a wall that happens to
                     * have a gap under it embeds the actor in terrain, where
                     * blocked_h refuses every move and the room soft-locks.
                     * Only the snapped case needs this: when cx == a->x the
                     * actor is already standing there. */
                    may_v = 1;
                    if (cx != a->x) {
                        if (solid_move(obj_px(cx, a->y)))
                            may_v = 0;
                    }
                    if (may_v) {
                        a->x      = cx;
                        a->y      = (uint8_t)(below_y - 6);
                        a->state  = ST_LADDER;
                        a->dir_v  = 1;
                        a->dir_h  = 0;
                        a->sprite = GLYPH_CLIMBER1;
                        return;
                    }
                }
            }
        }
    }

    /* Loss of support stays on exact column boundaries for everyone, so
     * snapping cannot change when an actor starts to fall. Standing in a
     * ladder cell never falls, and the bottom row is always solid ground. */
    if (off == 0) {
        cur     = obj_px(a->x, a->y);
        below_y = row_below(a->y);
        if (cur != OBJ_LADDER && below_y < (ROOM_ROWS * 8) &&
            !supports(obj_px(a->x, below_y))) {
            a->y      = (uint8_t)(below_y - 6);
            a->state  = ST_FALLING;
            a->dir_v  = 1;
            a->dir_h  = 0;
            a->sprite = GLYPH_MAN;
            return;
        }
    }

    if (in_h == 0)
        return;

    if (in_h < 0) {
        if (a->x < 2) return;
        nx = (uint8_t)(a->x - 2);
    } else {
        if (a->x >= 248) return;
        nx = (uint8_t)(a->x + 2);
    }

    if (blocked_h(nx, a->y))
        return;

    a->dir_h = in_h;
    a->dir_v = 0;

    /* Walking into a rope grabs it. The original always starts from the
     * same rope frame here regardless of travel direction. */
    if (obj_px(nx, a->y) == OBJ_ROPE) {
        a->x      = nx;
        a->state  = ST_ROPE;
        a->dir_v  = 0;
        a->sprite = (uint8_t)(GLYPH_ROPEB1 + 2);
        return;
    }

    a->x = nx;

    /* Walking off an edge starts a fall, unless the new cell is itself a
     * ladder, which the player can hang on. */
    below_y = row_below(a->y);
    if (below_y < (ROOM_ROWS * 8) && !supported_at(a->x, below_y) &&
        obj_px(a->x, a->y) != OBJ_LADDER) {
        a->y      = (uint8_t)(below_y - 6);
        a->state  = ST_FALLING;
        a->dir_v  = 1;
        a->sprite = GLYPH_MAN;
        return;
    }

    a->frame = (uint8_t)((a->frame + 1) % 3);
    a->sprite = (uint8_t)((in_h > 0 ? GLYPH_RIGHT1 : GLYPH_LEFT1)
                              + a->frame);
}



static void state_falling(Actor *a)
{
    uint8_t below_y;

    a->dir_v  = 1;
    a->dir_h  = 0;
    a->frame  = 0;
    a->sprite = GLYPH_MAN;

    if (a->y >= 174) {
        /* Cannot descend past the floor of the room. */
        a->y     = (uint8_t)((ROOM_ROWS - 1) * 8);
        a->state = ST_WALKING;
        return;
    }

    a->y = (uint8_t)(a->y + 2);

    /* Landing and rope capture are only tested on row boundaries. */
    if ((a->y & 7) != 0)
        return;

    below_y = row_below(a->y);
    if (below_y >= (ROOM_ROWS * 8) || supported_at(a->x, below_y)) {
        a->state  = ST_WALKING;
        a->sprite = GLYPH_MAN;
        return;
    }

    if (obj_px(a->x, a->y) == OBJ_ROPE ||
        obj_px2(a->x, a->y) == OBJ_ROPE) {
        a->state  = ST_ROPE;
        a->sprite = GLYPH_ROPEA1;
    }
}

static void state_ladder(Actor *a, int8_t in_h, int8_t in_v)
{
    uint8_t ny, cur, below_y, off, cy, may_h;

    /* Stepping off sideways is judged at a rung boundary. The player climbs
     * 2 px at a time, so demanding an exact hit swallows three requests in
     * four, exactly as it does for digging and for entering a ladder. Snap
     * to the nearest rung instead, with a half-cell tie going the way the
     * player is already climbing. Guards get no tolerance -- their AI
     * aligns itself, and their markers are written on cell boundaries. */
    if (in_h != 0) {
        off   = (uint8_t)(a->y & 7);
        cy    = a->y;
        may_h = 0;
        if (off == 0)
            may_h = 1;
        if (off != 0 && !a->is_enemy) {
            cy = (uint8_t)(a->y - off);
            if (off > CLIMB_SNAP)
                cy = (uint8_t)(cy + 8);
            if (off == CLIMB_SNAP && a->dir_v > 0)
                cy = (uint8_t)(cy + 8);
            /* Snapping into terrain or out of the room would embed the
             * player in a wall, where every move is refused. */
            if (cy < (ROOM_ROWS * 8) && !solid_move(obj_px(a->x, cy)))
                may_h = 1;
        }

        /* The snap is only committed if the step-off really is afforded,
         * so the player is never dragged up or down for nothing. */
        if (may_h) {
            if (in_h < 0) {
                if (a->x >= 8 &&
                    !solid_move(obj_px((uint8_t)(a->x - 8), cy))) {
                    a->y      = cy;
                    a->state  = ST_WALKING;
                    a->dir_v  = 0;
                    a->dir_h  = -1;
                    a->sprite = GLYPH_RIGHT1;
                    return;
                }
            } else {
                if (a->x < 248 &&
                    !solid_move(obj_px((uint8_t)(a->x + 8), cy))) {
                    a->y      = cy;
                    a->state  = ST_WALKING;
                    a->dir_v  = 0;
                    a->dir_h  = 1;
                    a->sprite = GLYPH_RIGHT1;
                    return;
                }
            }
        }
    }

    if (in_v == 0)
        return;

    if (in_v < 0) {
        if (a->y < 2) return;
        ny = (uint8_t)(a->y - 2);
        /* Climbing up: the head cell is the one being entered. */
        if (solid_move(obj_px(a->x, ny)))
            return;
    } else {
        if (a->y >= 174) return;
        ny = (uint8_t)(a->y + 2);
        /* Climbing down: test the cell the *feet* enter, not the one the
         * head is still in, otherwise the move is only refused once the
         * head crosses the boundary and the actor ends up sunk up to 6 px
         * into the floor at a non-rung-aligned Y -- where stepping off
         * sideways is impossible. */
        if (solid_move(obj_px(a->x, (uint8_t)(ny + 7))))
            return;
    }

    a->y     = ny;
    a->dir_v = in_v;
    a->dir_h = 0;

    if ((a->y & 7) == 0) {
        cur = obj_px(a->x, a->y);

        if (cur == OBJ_ROPE) {
            a->state  = ST_ROPE;
            a->dir_v  = 0;
            a->dir_h  = 0;
            a->sprite = (uint8_t)(GLYPH_ROPEA1 + 2);
            return;
        }

        if (cur != OBJ_LADDER) {
            /* Climbed off the end of the ladder: stand if there is ground
             * here, otherwise drop. */
            below_y = row_below(a->y);
            if (below_y >= (ROOM_ROWS * 8) ||
                supports(obj_px(a->x, below_y))) {
                a->state  = ST_WALKING;
                a->dir_v  = 0;
                a->sprite = GLYPH_RIGHT1;
            } else {
                a->state  = ST_FALLING;
                a->dir_v  = 1;
                a->sprite = GLYPH_MAN;
            }
            return;
        }
    }

    a->frame  = (uint8_t)((a->frame + 1) & 1);
    a->sprite = (uint8_t)(GLYPH_CLIMBER1 + a->frame);
}

static void state_rope(Actor *a, int8_t in_h, int8_t in_v)
{
    uint8_t below_y, nx, cur, cur2;

    /* Letting go downwards. */
    if (in_v > 0) {
        below_y = row_below(a->y);
        if (below_y < (ROOM_ROWS * 8)) {
            if ((a->x & 7) == 0 &&
                obj_px(a->x, below_y) == OBJ_LADDER) {
                a->y      = (uint8_t)(below_y - 6);
                a->state  = ST_LADDER;
                a->dir_v  = 1;
                a->dir_h  = 0;
                a->sprite = GLYPH_CLIMBER1;
                return;
            }
            if (supported_at(a->x, below_y)) {
                a->state  = ST_WALKING;
                a->dir_v  = 0;
                a->sprite = (uint8_t)(GLYPH_RIGHT1 + 2);
                return;
            }
            a->y      = (uint8_t)(below_y - 6);
            a->state  = ST_FALLING;
            a->dir_v  = 1;
            a->sprite = GLYPH_MAN;
            return;
        }
    }

    if (in_h == 0)
        return;

    if (in_h < 0) {
        if (a->x < 2) return;
        nx = (uint8_t)(a->x - 2);
    } else {
        if (a->x >= 248) return;
        nx = (uint8_t)(a->x + 2);
    }

    if (blocked_h(nx, a->y))
        return;

    a->x     = nx;
    a->dir_h = in_h;
    a->dir_v = 0;

    cur  = obj_px(a->x, a->y);
    cur2 = obj_px2(a->x, a->y);

    /* Traversing onto a ladder takes hold of it. */
    if ((a->x & 7) == 0 && cur == OBJ_LADDER) {
        a->state  = ST_LADDER;
        a->sprite = GLYPH_CLIMBER1;
        return;
    }

    /* Still hanging while any occupied cell is rope. */
    if (cur != OBJ_ROPE && cur2 != OBJ_ROPE) {
        below_y = row_below(a->y);
        if (below_y >= (ROOM_ROWS * 8) || supported_at(a->x, below_y)) {
            a->state  = ST_WALKING;
            a->dir_v  = 0;
            a->sprite = (uint8_t)(GLYPH_RIGHT1 + 2);
        } else {
            a->y      = (uint8_t)(below_y - 6);
            a->state  = ST_FALLING;
            a->dir_v  = 1;
            a->sprite = GLYPH_MAN;
        }
        return;
    }

    a->frame  = (uint8_t)((a->frame + 1) % 3);
    a->sprite = (uint8_t)((in_h > 0 ? GLYPH_ROPEA1 : GLYPH_ROPEB1)
                              + a->frame);
}

/* ==================================================================
 * Digging and holes
 * ================================================================== */

static void holes_reset(void)
{
    uint8_t i;
    for (i = 0; i < HOLES_COUNT; i++)
        s_hole_life[i] = HOLE_FREE;
}

/* Start a dig in `dir`. The target is the gap beside the player; the brick
 * that is actually removed sits directly below it. */
static void dig_start(int8_t dir)
{
    uint8_t col, row, tcol, target, under, off, cx;

    if (player.state == ST_FALLING || s_dig_phase != DIG_IDLE)
        return;

    /* The original insists on an exactly aligned column here. The player
     * walks 2 px at a time, so that would swallow three presses in four.
     * Pick the nearest column boundary within DIG_SNAP instead, with a
     * half-cell tie going the way the dig faces, and judge that column. */
    off = (uint8_t)(player.x & 7);
    cx  = player.x;
    if (off != 0) {
        cx = (uint8_t)(player.x - off);
        if (off > DIG_SNAP)
            cx = (uint8_t)(cx + 8);
        if (off == DIG_SNAP && dir > 0)
            cx = (uint8_t)(cx + 8);
        /* Snapping into terrain would embed the player in a wall, where
         * blocked_h refuses every move and the room soft-locks. */
        if (solid_move(obj_px(cx, player.y)))
            return;
    }

    col = (uint8_t)(cx >> 3);
    row = (uint8_t)(player.y >> 3);

    if (dir < 0) {
        if (col == 0) return;
        tcol = (uint8_t)(col - 1);
    } else {
        if (col >= ROOM_COLS - 1) return;
        tcol = (uint8_t)(col + 1);
    }

    if (row + 1 >= ROOM_ROWS)
        return;

    target = room_obj(tcol, row);
    under  = room_obj(tcol, (uint8_t)(row + 1));

    /* A hidden ladder counts as empty space for this test. */
    if (target != OBJ_EMPTY && target != OBJ_HIDLADDER)
        return;
    if (under != OBJ_BRICKS)
        return;

    /* The snap is only committed once the dig is certain, so the player is
     * never dragged sideways for nothing. The frame erases the player from
     * its *drawn* position just before compositing, and this snap happens
     * before that, so there is nothing to clear here. */
    if (cx != player.x)
        player.x = cx;

    s_dig_col   = tcol;
    s_dig_row   = row;
    s_dig_phase = 0;
}

/* Advance the dig by one phase; on completion the brick is removed and a
 * hole slot starts its countdown. */
static void dig_update(void)
{
    uint8_t i, g;

    if (s_dig_phase == DIG_IDLE)
        return;

    if (s_dig_phase >= DIG_PHASES) {
        room_set_cell(s_dig_col, (uint8_t)(s_dig_row + 1), OBJ_EMPTY);
        for (i = 0; i < HOLES_COUNT; i++) {
            if (s_hole_life[i] == HOLE_FREE) {
                s_hole_life[i] = HOLE_LIFECYCLE;
                s_hole_col[i]  = s_dig_col;
                s_hole_row[i]  = s_dig_row;
                break;
            }
        }
        s_dig_phase = DIG_IDLE;
        return;
    }

    /* Each phase is a two-glyph vertical pair: debris in the gap above and
     * the brick breaking up below. */
    g = (uint8_t)(GLYPH_DIG_BASE + (s_dig_phase << 1));
    cell_draw(s_dig_col, s_dig_row, g, COL_BRRED, COL_BLACK);
    cell_draw(s_dig_col, (uint8_t)(s_dig_row + 1), (uint8_t)(g + 1),
              COL_BRRED, COL_BLACK);
    snd_dig(s_dig_phase);
    s_dig_phase++;
}

/* Holes tick once every fourth frame, so HOLE_LIFECYCLE of 40 is about
 * 3.2 seconds at 50 Hz. */
static void holes_update(void)
{
    uint8_t i, brow;

    for (i = 0; i < HOLES_COUNT; i++) {
        if (s_hole_life[i] == HOLE_FREE)
            continue;

        brow = (uint8_t)(s_hole_row[i] + 1);

        if (s_hole_life[i] == 0) {
            room_set_cell(s_hole_col[i], brow, OBJ_BRICKS);
            s_hole_life[i] = HOLE_FREE;
            continue;
        }

        s_hole_life[i]--;

        /* Only the last four ticks show the brick growing back. */
        if (s_hole_life[i] < 4)
            cell_draw(s_hole_col[i], brow,
                      (uint8_t)(GLYPH_FILL_BASE + (3 - s_hole_life[i])),
                      COL_BRRED, COL_BLACK);
    }
}

/* ==================================================================
 * Scoring
 * ================================================================== */

void game_add_score(uint16_t points)
{
    score += points;
    if (score > 9999999UL)
        score = 9999999UL;
    hud_set_score(score);
}

void game_level_bonus(void)
{
    uint8_t i;
    /* The original awards SCORE_BONUS thirty times, one per beeper step,
     * so the sweep and the climbing score stay locked together. */
    for (i = 0; i < LEVEL_BONUS_STEPS; i++) {
        snd_exit_step(i);
        game_add_score(SCORE_BONUS);
    }
}

/* ==================================================================
 * Guards
 *
 * Guards reuse the player's state handlers, exactly as the original shares
 * one state machine between every MAN record. All a guard adds is a policy
 * that turns the chase into the same (in_h, in_v) pair the player's keys
 * produce, plus occupancy, trapping and gold handling.
 * ================================================================== */

Actor enemies[MAX_ENEMIES];
uint8_t enemy_count;

/* Randomness adapter. The original walks a pointer through the Spectrum
 * ROM; nothing of that is portable, so this is an ordinary 16-bit xorshift
 * used for the same three decisions: gold pickup, gold drop and respawn. */
static uint16_t s_rnd = 0xACE1;

static uint8_t game_rnd(void)
{
    s_rnd ^= (uint16_t)(s_rnd << 7);
    s_rnd ^= (uint16_t)(s_rnd >> 9);
    s_rnd ^= (uint16_t)(s_rnd << 8);
    return (uint8_t)s_rnd;
}

/* ---- Occupancy ----
 * A guard standing on an otherwise empty cell marks it SOLID in the
 * collision grid so other guards treat it as a wall and never overlap. The
 * marker is written with room_set_logical, so it never reaches the visual
 * layer and no solid block is ever drawn under a guard. */

static void enemy_unmark(Actor *e)
{
    if (e->under == NOT_MARKED)
        return;

    /* Only restore if our marker is still there. If the cell now holds
     * something else the terrain changed underneath us -- a hole refilling
     * into BRICKS -- and that must not be overwritten. */
    if (room_obj(e->mark_col, e->mark_row) == OBJ_SOLID) {
        if (e->under == OBJ_HIDLADDER && boxes_left == 0)
            /* The exit ladder appeared while we were standing on it. */
            room_set_cell(e->mark_col, e->mark_row, OBJ_LADDER);
        else
            room_set_logical(e->mark_col, e->mark_row, e->under);
    }
    e->under = NOT_MARKED;
}

static void enemy_mark(Actor *e)
{
    uint8_t col, row, o;

    if ((e->x & 7) != 0 || (e->y & 7) != 0)
        return;

    col = (uint8_t)(e->x >> 3);
    row = (uint8_t)(e->y >> 3);
    o   = room_obj(col, row);

    /* The original marks empty cells and hidden ladders only. */
    if (o != OBJ_EMPTY && o != OBJ_HIDLADDER)
        return;

    e->under    = o;
    e->mark_col = col;
    e->mark_row = row;
    room_set_logical(col, row, OBJ_SOLID);
}

/* ---- Gold ---- */

/* Put a carried piece of gold back on the map. Tries the guard's own cell
 * first, then any empty cell, so carried gold can never be lost -- losing
 * it would leave boxes_left above zero with no way to collect it and make
 * the room impossible to finish. */
static void enemy_drop_box(Actor *e, uint8_t col, uint8_t row)
{
    uint8_t r, c;

    e->has_box = 0;

    if (room_obj(col, row) == OBJ_EMPTY) {
        room_set_cell(col, row, OBJ_BOX);
        return;
    }
    for (r = 0; r < ROOM_ROWS; r++) {
        for (c = 0; c < ROOM_COLS; c++) {
            if (room_obj(c, r) == OBJ_EMPTY) {
                room_set_cell(c, r, OBJ_BOX);
                return;
            }
        }
    }
    /* Nowhere at all to put it: count it as collected rather than wedge
     * the room. Not reachable in any shipped level. */
    if (boxes_left != 0)
        boxes_left--;
}

/* ---- Death and respawn ---- */

/* Try to place a respawning guard on one cell. Returns 1 if it took.
 * Written as sequential ifs rather than a chain of ||, which sccz80 has
 * already been seen to miscompile in this project. */
static uint8_t enemy_respawn_at(Actor *e, uint8_t col, uint8_t row)
{
    uint8_t o, ok;

    o  = room_obj(col, row);
    ok = 0;
    if (o == OBJ_EMPTY)     ok = 1;
    if (o == OBJ_HIDLADDER) ok = 1;
    if (o == OBJ_BOX)       ok = 1;
    if (o == OBJ_ROPE)      ok = 1;
    if (!ok)
        return 0;

    e->state = ST_WALKING;
    if (o == OBJ_ROPE)
        e->state = ST_ROPE;
    e->x          = (uint8_t)(col << 3);
    e->y          = (uint8_t)(row << 3);
    e->dir_h      = 0;
    e->dir_v      = 0;
    e->frame      = 0;
    e->sprite     = GLYPH_MAN;
    e->trap_timer = 0;
    e->under      = NOT_MARKED;
    return 1;
}

static void enemy_respawn(Actor *e)
{
    uint8_t row, col, o, tries, cycles;

    enemy_unmark(e);
    if (e->has_box)
        enemy_drop_box(e, (uint8_t)(e->x >> 3), (uint8_t)(e->y >> 3));

    game_add_score(SCORE_KILL);

    /* The original searches rows 0..3, ten random columns per row, and
     * loops for ever. Bound it: a room whose top four rows are solid would
     * otherwise hang the game, and the sweep below always terminates. */
    for (cycles = 0; cycles < 8; cycles++) {
        for (row = 0; row < 4; row++) {
            for (tries = 0; tries < 10; tries++) {
                col = (uint8_t)(game_rnd() & 31);
                if (enemy_respawn_at(e, col, row))
                    return;
            }
        }
    }

    /* Nothing came up by chance: sweep the top rows, then the whole room. */
    for (row = 0; row < ROOM_ROWS; row++) {
        for (col = 0; col < ROOM_COLS; col++) {
            if (enemy_respawn_at(e, col, row))
                return;
        }
    }

    /* A room with no free cell at all cannot happen in the shipped levels;
     * leave the guard where it is rather than invent a position. */
    e->state      = ST_WALKING;
    e->trap_timer = 0;
    e->under      = NOT_MARKED;
}

/* ---- Movement policy ----
 * A local greedy chase, as in the original: there is no route scoring, only
 * a preference for closing the vertical gap when a ladder allows it, then
 * horizontal pursuit, with a turn when the way ahead is blocked. */

static uint8_t enemy_blocked_h(const Actor *e, int8_t dir)
{
    uint8_t nx;

    if (dir < 0) {
        if (e->x < 2) return 1;
        nx = (uint8_t)(e->x - 2);
    } else {
        if (e->x >= 248) return 1;
        nx = (uint8_t)(e->x + 2);
    }
    return blocked_h(nx, e->y);
}

static void enemy_think(Actor *e, int8_t *out_h, int8_t *out_v)
{
    int8_t dh, dv;
    uint8_t cur, below_y, below;

    *out_h = 0;
    *out_v = 0;

    if (e->state == ST_FALLING)
        return;                 /* no steering while falling */

    dh = 0;
    dv = 0;
    if (player.x > e->x) dh = 1;
    if (player.x < e->x) dh = -1;
    if (player.y > e->y) dv = 1;
    if (player.y < e->y) dv = -1;

    if (e->state == ST_LADDER) {
        /* Keep climbing toward the player; step off when level with them. */
        if (dv != 0) { *out_v = dv; return; }
        *out_h = dh;
        return;
    }

    if (e->state == ST_ROPE) {
        /* Drop off the rope when the player is below, else slide along. */
        if (dv > 0) { *out_v = 1; return; }
        *out_h = dh;
        return;
    }

    /* Walking. Vertical moves are only available on a column boundary. */
    if ((e->x & 7) == 0) {
        cur = obj_px(e->x, e->y);

        if (dv < 0 && cur == OBJ_LADDER && e->y >= 8 &&
            !solid_move(obj_px(e->x, (uint8_t)(e->y - 8)))) {
            *out_v = -1;
            return;
        }

        if (dv > 0) {
            below_y = row_below(e->y);
            if (below_y < (ROOM_ROWS * 8)) {
                below = obj_px(e->x, below_y);
                if (below == OBJ_LADDER || below == OBJ_HIDLADDER) {
                    *out_v = 1;
                    return;
                }
            }
        }
    }

    /* Horizontal pursuit, preferring the player's side, then the direction
     * already being travelled, then the reverse. */
    if (dh != 0 && !enemy_blocked_h(e, dh)) {
        e->turn = dh;
        *out_h  = dh;
        return;
    }
    if (e->turn != 0 && !enemy_blocked_h(e, e->turn)) {
        *out_h = e->turn;
        return;
    }
    if (e->turn != 0 && !enemy_blocked_h(e, (int8_t)-e->turn)) {
        e->turn = (int8_t)-e->turn;
        *out_h  = e->turn;
    }
}

/* ---- Trapping ---- */

/* Pixel position of the pit a dug hole leaves: the brick that was removed
 * sits one row below the recorded gap. */
static uint8_t enemy_in_hole(const Actor *e)
{
    uint8_t i;

    if ((e->x & 7) != 0 || (e->y & 7) != 0)
        return 0;

    for (i = 0; i < HOLES_COUNT; i++) {
        if (s_hole_life[i] == HOLE_FREE)
            continue;
        if (s_hole_col[i] == (uint8_t)(e->x >> 3) &&
            (uint8_t)(s_hole_row[i] + 1) == (uint8_t)(e->y >> 3))
            return 1;
    }
    return 0;
}

/* Run one turn for a guard that is sitting in a hole. */
static void enemy_trapped(Actor *e)
{
    uint8_t col, row;
    int8_t  side;

    col = (uint8_t)(e->x >> 3);
    row = (uint8_t)(e->y >> 3);

    /* The hole closing on a trapped guard kills it. The occupancy marker
     * has already been lifted by the caller, so this reads real terrain. */
    if (room_obj(col, row) == OBJ_BRICKS) {
        enemy_respawn(e);
        return;
    }

    if (e->trap_timer != 0)
        e->trap_timer--;

    if (e->trap_timer != 0)
        return;

    /* Climb out diagonally onto the floor beside the hole, preferring the
     * player's side. Going straight up would drop the guard back in. */
    side = (player.x < e->x) ? -1 : 1;

    if (!solid_move(room_obj((uint8_t)(col + side), (uint8_t)(row - 1))) &&
        supports(room_obj((uint8_t)(col + side), row))) {
        e->x = (uint8_t)((col + side) << 3);
    } else {
        side = (int8_t)-side;
        if (!solid_move(room_obj((uint8_t)(col + side), (uint8_t)(row - 1))) &&
            supports(room_obj((uint8_t)(col + side), row))) {
            e->x = (uint8_t)((col + side) << 3);
        } else {
            /* Walled in on both sides: keep waiting. */
            e->trap_timer = 1;
            return;
        }
    }

    e->y      = (uint8_t)((row - 1) << 3);
    e->state  = ST_WALKING;
    e->dir_h  = side;
    e->dir_v  = 0;
    e->sprite = GLYPH_MAN;
    e->turn   = side;
}

/* ---- One guard turn ---- */

static void enemy_update(Actor *e)
{
    int8_t  in_h, in_v;
    uint8_t col, row, o;

    /* Lift our own occupancy marker first: every terrain test below, and
     * the state handlers themselves, must see the real map and not be
     * blocked by the cell we are standing on. */
    enemy_unmark(e);

    if (e->state == ST_TRAPPED) {
        enemy_trapped(e);
    } else {
        /* A hole refilling on top of a guard crushes it, as it does the
         * player. */
        if (obj_px(e->x, e->y) == OBJ_BRICKS) {
            enemy_respawn(e);
            enemy_mark(e);
            return;
        }

        enemy_think(e, &in_h, &in_v);

        switch (e->state) {
        case ST_WALKING: state_walking(e, in_h, in_v); break;
        case ST_FALLING: state_falling(e);             break;
        case ST_LADDER:  state_ladder(e, in_h, in_v);  break;
        case ST_ROPE:    state_rope(e, in_h, in_v);    break;
        default:         e->state = ST_WALKING;        break;
        }

        if (enemy_in_hole(e)) {
            e->state      = ST_TRAPPED;
            e->trap_timer = TRAP_TIME;
            e->dir_h      = 0;
            e->dir_v      = 0;
            e->sprite     = GLYPH_MAN;
            game_add_score(SCORE_TRAP);
        } else if (e->state == ST_WALKING &&
                   (e->x & 7) == 0 && (e->y & 7) == 0) {
            /* Gold: picked up half the time, put down one time in eight.
             * Neither changes boxes_left -- carried gold still counts as
             * outstanding, so the player must make the guard give it up. */
            col = (uint8_t)(e->x >> 3);
            row = (uint8_t)(e->y >> 3);
            o   = room_obj(col, row);

            if (e->has_box == 0) {
                if (o == OBJ_BOX && (game_rnd() & GUARD_PICKUP_MASK) == 0) {
                    room_set_cell(col, row, OBJ_EMPTY);
                    e->has_box = 1;
                }
            } else {
                if (o == OBJ_EMPTY && (game_rnd() & GUARD_DROP_MASK) == 0) {
                    room_set_cell(col, row, OBJ_BOX);
                    e->has_box = 0;
                }
            }
        }
    }

    enemy_mark(e);
}

/* Has any guard caught the player? The original's window is five pixels on
 * each axis, tested before the player moves. */
static uint8_t enemy_caught_player(void)
{
    uint8_t i, d;

    for (i = 0; i < enemy_count; i++) {
        d = (enemies[i].x > player.x)
            ? (uint8_t)(enemies[i].x - player.x)
            : (uint8_t)(player.x - enemies[i].x);
        if (d >= CATCH_RANGE)
            continue;

        d = (enemies[i].y > player.y)
            ? (uint8_t)(enemies[i].y - player.y)
            : (uint8_t)(player.y - enemies[i].y);
        if (d < CATCH_RANGE)
            return 1;
    }
    return 0;
}

/* ==================================================================
 * Frame
 * ================================================================== */

void game_start_room(uint8_t room)
{
    Spawn sp;
    uint8_t i;

    s_room = room;
    room_unpack(room);
    room_present(RENDER_GAME);
    room_spawn(room, &sp);

    boxes_left  = room_count_boxes();
    s_timer     = 0;
    s_dig_phase = DIG_IDLE;
    holes_reset();

    player.state    = ST_WALKING;
    player.x        = (uint8_t)(sp.player_col << 3);
    player.y        = (uint8_t)(sp.player_row << 3);
    player.dir_h    = 0;
    player.dir_v    = 0;
    player.frame    = 0;
    player.sprite   = GLYPH_MAN;
    player.is_enemy = 0;
    player.has_box  = 0;
    player.under    = NOT_MARKED;

    enemy_count = sp.enemy_count;
    if (enemy_count > MAX_ENEMIES)
        enemy_count = MAX_ENEMIES;

    for (i = 0; i < enemy_count; i++) {
        enemies[i].state      = ST_WALKING;
        enemies[i].x          = (uint8_t)(sp.enemy_col[i] << 3);
        enemies[i].y          = (uint8_t)(sp.enemy_row[i] << 3);
        enemies[i].dir_v      = 0;
        enemies[i].frame      = 0;
        enemies[i].sprite     = GLYPH_MAN;
        enemies[i].is_enemy   = 1;
        enemies[i].has_box    = 0;
        enemies[i].under      = NOT_MARKED;
        enemies[i].trap_timer = 0;
        /* The original aims each guard at the player's column to begin. */
        enemies[i].dir_h      = (enemies[i].x > player.x) ? -1 : 1;
        enemies[i].turn       = enemies[i].dir_h;
        enemy_mark(&enemies[i]);
    }

    hud_draw(room, s_lives, "0000000");
    hud_set_score(score);
    hud_set_cheat(s_cheat);

    /* The room is freshly painted, so adopt these placements as composited
     * rather than erasing whatever the previous room left behind. */
    actor_place(&player);
    actor_draw(&player);
    for (i = 0; i < enemy_count; i++) {
        actor_place(&enemies[i]);
        actor_draw(&enemies[i]);
    }
}

/* The player is standing inside a brick that has just grown back. Returns 1
 * when that is fatal. Under the cheat it is not: the player is lifted into
 * the gap the hole was dug from, which is the cell directly above and is
 * therefore known to be clear. Leaving them embedded would block every
 * move and soft-lock the run, which is not what "cannot die" should mean. */
static uint8_t crushed(void)
{
    if (s_cheat == 0)
        return 1;
    if (player.y < 8)
        return 0;

    player.y = (uint8_t)((player.y & 0xF8) - 8);
    return 0;
}

uint8_t game_frame(void)
{
    int8_t in_h, in_v;
    uint8_t col, row, i;

    s_timer++;
    input_poll();

    if (input_shell_pressed(SBIT_QUIT))
        return PLAY_QUIT;

    /* P holds the room still. Nothing is redrawn while paused, so the
     * screen simply stops; the same key releases it. */
    if (input_pressed(KBIT_PAUSE)) {
        for (;;) {
            wait_vsync();
            input_poll();
            if (input_pressed(KBIT_PAUSE))
                break;
            if (input_shell_pressed(SBIT_QUIT))
                return PLAY_QUIT;
        }
    }

    /* A hole refilling around the player crushes them. Split into separate
     * statements for the same sccz80 reason as blocked_h().
     *
     * Under the cheat the brick does not kill, but the player must not be
     * left embedded in it either: being inside terrain blocks every move
     * and would soft-lock the run. Lifting them into the gap above is what
     * the refilling brick would physically do, and that cell is the one the
     * hole was dug out of, so it is known to be clear. */
    if (obj_px(player.x, player.y) == OBJ_BRICKS) {
        if (crushed() != 0)
            return PLAY_DIED;
    }
    if (obj_px2(player.x, player.y) == OBJ_BRICKS) {
        if (crushed() != 0)
            return PLAY_DIED;
    }

    /* The original tests the guards before moving the player. */
    if (enemy_caught_player()) {
        if (s_cheat == 0)
            return PLAY_DIED;
    }

    in_h = 0;
    in_v = 0;
    if (player.state != ST_FALLING) {
        if (input_held(KBIT_LEFT))  in_h = -1;
        if (input_held(KBIT_RIGHT)) in_h = 1;
        if (input_held(KBIT_UP))    in_v = -1;
        if (input_held(KBIT_DOWN))  in_v = 1;
    }

    switch (player.state) {
    case ST_WALKING: state_walking(&player, in_h, in_v); break;
    case ST_FALLING: state_falling(&player);           break;
    case ST_LADDER:  state_ladder(&player, in_h, in_v);  break;
    case ST_ROPE:    state_rope(&player, in_h, in_v);    break;
    default:         player.state = ST_WALKING; break;
    }

    /* Gold is only picked up when the player sits squarely on the cell. */
    if ((player.x & 7) == 0 && (player.y & 7) == 0) {
        col = (uint8_t)(player.x >> 3);
        row = (uint8_t)(player.y >> 3);
        if (room_obj(col, row) == OBJ_BOX) {
            room_set_cell(col, row, OBJ_EMPTY);
            game_add_score(SCORE_BOX);
            snd_gold();
            if (boxes_left != 0)
                boxes_left--;
            if (boxes_left == 0)
                room_transform_ladders();
        }
    }

    /* Digging is edge-triggered and keeps its direction between presses. */
    if (input_pressed(KBIT_DIG)) {
        if (player.dir_h != 0)
            s_dig_dir = player.dir_h;
        dig_start(s_dig_dir);
    }

    dig_update();
    if ((s_timer & HOLE_TICK_MASK) == 0)
        holes_update();

    /* Guards move at half the player's rate, and the two halves of the
     * set are spread across alternate frames the way the original's
     * (index XOR GAME_TIMER) & 1 gate does. */
    for (i = 0; i < enemy_count; i++) {
        if (((i ^ (uint8_t)s_timer) & 1) != 0)
            enemy_update(&enemies[i]);
    }

    /* Present the frame.
     *
     * All drawing is deferred to here and fenced behind the vertical blank,
     * so compositing starts at a consistent point in the display cycle
     * instead of landing wherever the previous frame's work happened to
     * finish. Each actor's erase sits immediately before its own draw
     * inside actor_composite(). */
    wait_vsync();

    actor_composite(&player);
    for (i = 0; i < enemy_count; i++)
        actor_composite(&enemies[i]);

    /* Being caught can also happen on the guards' own move. */
    if (enemy_caught_player()) {
        if (s_cheat == 0)
            return PLAY_DIED;
    }

    /* The level is finished by reaching the top row with no gold left. */
    if (boxes_left == 0 && player.y == 0)
        return PLAY_COMPLETE;

    return PLAY_RUNNING;
}

uint8_t game_lives(void)
{
    return s_lives;
}

void game_set_lives(uint8_t lives)
{
    s_lives = lives;
    hud_set_lives(lives);
}

uint8_t game_cheat(void)
{
    return s_cheat;
}

void game_set_cheat(uint8_t on)
{
    s_cheat = on;
}
