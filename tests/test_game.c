/* Tests for the guard rules, the hole lifecycle, gold accounting and the
 * movement snap tolerances.
 *
 * Built as a CP/M program with the same sccz80 toolchain as the game and
 * run under MAME with no host input. Results are left in test_results[] for
 * the driver to read out of memory, which avoids having to read numbers back
 * off the screen.
 *
 * Each check appends one byte: 1 for a pass, 0 for a failure. tools\mame.ps1
 * peeks the buffer and tests\run.ps1 pairs the bytes with the check names in
 * source order.
 */

#include "../src/c/defs.h"
#include "../src/c/game.h"
#include "../src/c/room.h"
#include "../src/c/input.h"

extern uint8_t test_keys;
extern uint8_t unpacked_room[];

/* Read back by the driver. Keep TEST_MAX and the array in step: checks past
 * the end are silently dropped, which reads as "the suite stopped early". */
#define TEST_MAX 96
uint8_t test_results[TEST_MAX];
uint8_t test_count;
uint8_t test_fail;
uint8_t test_done;

static void check(uint8_t ok)
{
    if (test_count < TEST_MAX) {
        test_results[test_count] = ok ? 1 : 0;
        test_count++;
    }
    if (!ok)
        test_fail++;
}

static uint16_t count_obj(uint8_t obj)
{
    uint16_t i, n;

    n = 0;
    for (i = 0; i < ROOM_CELLS; i++) {
        if (unpacked_room[i] == obj)
            n++;
    }
    return n;
}

/* Gold still outstanding: what lies on the map plus what guards carry. */
static uint16_t gold_outstanding(void)
{
    uint16_t n;
    uint8_t i;

    n = count_obj(OBJ_BOX);
    for (i = 0; i < enemy_count; i++) {
        if (enemies[i].has_box)
            n++;
    }
    return n;
}

static void place(Actor *a, uint8_t col, uint8_t row)
{
    a->state      = ST_WALKING;
    a->x          = (uint8_t)(col << 3);
    a->y          = (uint8_t)(row << 3);
    a->dir_h      = 0;
    a->dir_v      = 0;
    a->frame      = 0;
    a->sprite     = GLYPH_MAN;
    a->has_box    = 0;
    a->under      = NOT_MARKED;
    a->trap_timer = 0;
    a->turn       = 1;
}

/* Dig through the floor to the player's left from an aligned column, with
 * no guards about. Room 1 has brick along row 6 from column 2 to 8, so
 * standing at (9, 5) and digging left targets column 8. Returns 1 on
 * success. */
static uint8_t dig_left_hole(void)
{
    uint8_t i;

    place(&player, 9, 5);
    player.dir_h = -1;

    test_keys = 0;
    game_frame();               /* settle the edge detector */
    test_keys = KBIT_DIG;

    for (i = 0; i < 60; i++) {
        game_frame();
        test_keys = 0;          /* digging is edge triggered */
        if (room_obj(8, 6) != OBJ_BRICKS)
            return 1;
    }
    return 0;
}

/* Walk a guard placed next to the hole into it. Returns 1 once trapped. */
static uint8_t trap_guard(void)
{
    uint8_t i;

    enemy_count = 1;
    place(&enemies[0], 8, 5);

    for (i = 0; i < 100; i++) {
        game_frame();
        if (enemies[0].state == ST_TRAPPED)
            return 1;
    }
    return 0;
}

/* Hand a guard a piece of gold the way a pickup would: take it off the map
 * so the total outstanding is unchanged. */
static uint8_t give_guard_gold(uint8_t i)
{
    uint8_t col, row;

    for (row = 0; row < ROOM_ROWS; row++) {
        for (col = 0; col < ROOM_COLS; col++) {
            if (room_obj(col, row) == OBJ_BOX) {
                room_set_cell(col, row, OBJ_EMPTY);
                enemies[i].has_box = 1;
                return 1;
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- */

/* 1-4: the catch window */
static void test_catch(void)
{
    game_set_lives(5);
    game_start_room(1);
    test_keys = 0;
    check(enemy_count == 3);

    place(&enemies[0], (uint8_t)(player.x >> 3), (uint8_t)(player.y >> 3));
    check(game_frame() == PLAY_DIED);

    /* A full cell away. The guard closes by two pixels on its own move, so
     * it is still outside the window when the frame's second catch test
     * runs -- which is why this uses a cell rather than the exact boundary. */
    game_start_room(1);
    enemy_count = 1;
    place(&enemies[0], 0, 0);
    enemies[0].x = (uint8_t)(player.x + 8);
    enemies[0].y = player.y;
    check(game_frame() == PLAY_RUNNING);

    enemies[0].x = (uint8_t)(player.x + CATCH_RANGE - 2);
    enemies[0].y = player.y;
    check(game_frame() == PLAY_DIED);
}

/* 5-9: digging, trapping and escaping */
static void test_trap_and_escape(void)
{
    uint8_t i;
    uint32_t before;

    game_set_lives(5);
    game_start_room(1);
    enemy_count = 0;

    check(dig_left_hole());
    check(room_obj(8, 6) != OBJ_BRICKS);

    before = score;
    check(trap_guard());
    check(score == before + SCORE_TRAP);

    /* The guard must not sit there for ever: either it climbs out or the
     * hole refills on it, and both end the trapped state. */
    for (i = 0; i < 250; i++) {
        game_frame();
        if (enemies[0].state != ST_TRAPPED)
            break;
    }
    check(enemies[0].state != ST_TRAPPED);
}

/* 10-13: a refilling hole kills the guard in it */
static void test_crush(void)
{
    uint16_t i;
    uint32_t before;

    game_set_lives(5);
    game_start_room(1);
    enemy_count = 0;

    check(dig_left_hole());
    check(trap_guard());

    /* Hold the countdown so the brick wins the race. */
    before = score;
    for (i = 0; i < 900; i++) {
        if (enemies[0].state == ST_TRAPPED)
            enemies[0].trap_timer = TRAP_TIME;
        game_frame();
        if (score >= before + SCORE_KILL)
            break;
    }
    check(score >= before + SCORE_KILL);
    check(enemies[0].y <= 3 * 8);
}

/* 14-17: guard-carried gold is never lost */
static void test_guard_gold(void)
{
    uint16_t i, boxes;

    game_set_lives(5);
    game_start_room(1);
    test_keys = 0;

    boxes = boxes_left;
    check(gold_outstanding() == boxes);

    check(give_guard_gold(0));

    for (i = 0; i < 300; i++) {
        game_frame();
        if (boxes_left != boxes)
            break;
    }
    check(boxes_left == boxes);
    check(gold_outstanding() == boxes);
}

/* 18-23: killing a guard that carries gold returns the gold to the map */
static void test_respawn_returns_gold(void)
{
    uint16_t i, boxes;

    game_set_lives(5);
    game_start_room(1);
    enemy_count = 0;

    check(dig_left_hole());
    check(trap_guard());

    boxes = boxes_left;
    check(give_guard_gold(0));

    for (i = 0; i < 900; i++) {
        if (enemies[0].state == ST_TRAPPED)
            enemies[0].trap_timer = TRAP_TIME;
        game_frame();
        if (enemies[0].has_box == 0)
            break;
    }
    check(enemies[0].has_box == 0);
    check(boxes_left == boxes);
    check(gold_outstanding() == boxes);
}

/* 24: occupancy markers never leak into the terrain */
static void test_occupancy_balanced(void)
{
    uint16_t solid_before, solid_after, i;
    uint8_t e;

    room_unpack(1);
    solid_before = count_obj(OBJ_SOLID);

    game_set_lives(5);
    game_start_room(1);
    test_keys = 0;
    for (i = 0; i < 200; i++)
        game_frame();

    /* Lift every live marker; what remains must be the original terrain. */
    for (e = 0; e < enemy_count; e++) {
        if (enemies[e].under != NOT_MARKED &&
            room_obj(enemies[e].mark_col, enemies[e].mark_row) == OBJ_SOLID) {
            room_set_logical(enemies[e].mark_col, enemies[e].mark_row,
                             enemies[e].under);
        }
    }
    solid_after = count_obj(OBJ_SOLID);
    check(solid_after == solid_before);
}

/* 25-26: finishing a room */
static void test_level_completion(void)
{
    game_set_lives(5);
    game_start_room(1);
    enemy_count = 0;
    test_keys = 0;

    check(game_frame() == PLAY_RUNNING);

    /* Row 0 is wall above the player's spawn column, and standing in brick
     * is fatal, so finish from a clear cell on the top row. */
    boxes_left = 0;
    player.x = 20 * 8;
    player.y = 0;
    check(game_frame() == PLAY_COMPLETE);
}

/* 27: the completion bonus */
static void test_level_bonus(void)
{
    uint32_t before;

    game_start_room(1);
    before = score;
    game_level_bonus();
    check(score == before + (uint32_t)LEVEL_BONUS_STEPS * SCORE_BONUS);
}

/* 28-29: the player collecting gold */
static void test_player_gold(void)
{
    uint16_t boxes;
    uint32_t before;
    uint8_t col, row;

    game_start_room(1);
    enemy_count = 0;
    test_keys = 0;

    col = (uint8_t)(player.x >> 3);
    row = (uint8_t)(player.y >> 3);
    room_set_cell(col, row, OBJ_BOX);
    boxes_left++;
    boxes = boxes_left;
    before = score;

    game_frame();
    check(score == before + SCORE_BOX);
    check(boxes_left == boxes - 1);
}

/* 30-33: the busiest rooms. Some room carries the maximum of five guards, and
 * guards sharing a pixel position are what produces sprite corruption.
 * The room is located by scanning rather than hard-coded, because the shipped
 * room set is a subset of the original and renumbers whenever it changes. */
static uint8_t busiest_room(void)
{
    uint8_t r;

    for (r = 1; r <= ROOM_COUNT; r++) {
        game_start_room(r);
        if (enemy_count == MAX_ENEMIES)
            return r;
    }
    return 0;
}

static void test_five_guards(void)
{
    uint16_t i, solid_before, solid_after;
    uint8_t a, b, overlap, out_of_bounds, room;

    game_set_lives(5);
    room = busiest_room();

    room_unpack(room);
    solid_before = count_obj(OBJ_SOLID);

    game_set_lives(5);
    game_start_room(room);
    test_keys = 0;
    check(room != 0 && enemy_count == MAX_ENEMIES);

    overlap = 0;
    out_of_bounds = 0;
    for (i = 0; i < 400; i++) {
        game_frame();
        for (a = 0; a < enemy_count; a++) {
            if (enemies[a].x > 248)
                out_of_bounds = 1;
            if (enemies[a].y > (ROOM_ROWS - 1) * 8)
                out_of_bounds = 1;
            for (b = (uint8_t)(a + 1); b < enemy_count; b++) {
                if (enemies[a].x == enemies[b].x &&
                    enemies[a].y == enemies[b].y)
                    overlap = 1;
            }
        }
    }
    check(!overlap);
    check(!out_of_bounds);

    for (a = 0; a < enemy_count; a++) {
        if (enemies[a].under != NOT_MARKED &&
            room_obj(enemies[a].mark_col, enemies[a].mark_row) == OBJ_SOLID) {
            room_set_logical(enemies[a].mark_col, enemies[a].mark_row,
                             enemies[a].under);
        }
    }
    solid_after = count_obj(OBJ_SOLID);
    check(solid_after == solid_before);
}

/* 34-38: the cheat (0 on the title screen) */
static void test_cheat(void)
{
    /* A guard standing on the player is lethal in a normal game... */
    game_set_cheat(0);
    game_set_lives(5);
    game_start_room(1);
    test_keys = 0;
    enemy_count = 1;
    place(&enemies[0], (uint8_t)(player.x >> 3), (uint8_t)(player.y >> 3));
    check(game_frame() == PLAY_DIED);

    /* ...and survivable with the cheat on. */
    game_set_cheat(1);
    game_start_room(1);
    test_keys = 0;
    enemy_count = 1;
    place(&enemies[0], (uint8_t)(player.x >> 3), (uint8_t)(player.y >> 3));
    check(game_frame() == PLAY_RUNNING);

    /* A brick closing over the player is lethal in a normal game... */
    game_set_cheat(0);
    game_start_room(1);
    test_keys = 0;
    enemy_count = 0;
    place(&player, 9, 5);
    room_set_cell(9, 5, OBJ_BRICKS);
    check(game_frame() == PLAY_DIED);

    /* ...and with the cheat it lifts the player into the cell above rather
     * than leaving them embedded, which would block every move. */
    game_set_cheat(1);
    game_start_room(1);
    test_keys = 0;
    enemy_count = 0;
    place(&player, 9, 5);
    room_set_cell(9, 5, OBJ_BRICKS);
    check(game_frame() == PLAY_RUNNING);
    check((uint8_t)(player.y >> 3) < 5);

    game_set_cheat(0);
}

/* 39-45: the training room (room 0) */
static void test_training_room(void)
{
    uint8_t col, row, top_ladder;

    game_set_cheat(0);
    game_set_lives(5);
    game_start_room(ROOM_TRAIN);
    test_keys = 0;

    /* Its own data, not room 1's, and the gold the generator reports. */
    check(boxes_left == 6);
    check(enemy_count == 1);

    /* The player must not start embedded in terrain or inside a guard. */
    col = (uint8_t)(player.x >> 3);
    row = (uint8_t)(player.y >> 3);
    check(room_obj(col, row) == OBJ_EMPTY);
    check(game_frame() == PLAY_RUNNING);

    /* A hidden ladder has to reach row 0 or the room cannot be completed,
     * and it must be hidden until the last piece of gold is taken. */
    top_ladder = 0;
    for (col = 0; col < ROOM_COLS; col++) {
        if (room_obj(col, 0) == OBJ_HIDLADDER)
            top_ladder = 1;
    }
    check(top_ladder == 1);

    room_transform_ladders();
    top_ladder = 0;
    for (col = 0; col < ROOM_COLS; col++) {
        if (room_obj(col, 0) == OBJ_LADDER)
            top_ladder = 1;
    }
    check(top_ladder == 1);

    /* The bottom row is bedrock, so nothing can be dug out of the world. */
    top_ladder = 1;
    for (col = 0; col < ROOM_COLS; col++) {
        if (room_obj(col, (uint8_t)(ROOM_ROWS - 1)) != OBJ_SOLID)
            top_ladder = 0;
    }
    check(top_ladder == 1);
}

/* Stand the player on the basement floor of the training room at pixel x,
 * facing dir, and ask to climb. Returns the resulting x, or 255 if no climb
 * started. Column 29 carries the ladder out of the basement, so x 232 is
 * dead on the ladder. */
static uint8_t climb_up_from(uint8_t x, int8_t dir)
{
    game_start_room(ROOM_TRAIN);
    enemy_count  = 0;
    player.state = ST_WALKING;
    player.x     = x;
    player.y     = 19 * 8;
    player.dir_h = dir;
    player.dir_v = 0;

    test_keys = KBIT_UP;
    game_frame();
    test_keys = 0;

    if (player.state != ST_LADDER)
        return 255;
    return player.x;
}

/* The same, one tier up, stepping down onto the head of that ladder. */
static uint8_t climb_down_from(uint8_t x, int8_t dir)
{
    game_start_room(ROOM_TRAIN);
    enemy_count  = 0;
    player.state = ST_WALKING;
    player.x     = x;
    player.y     = 14 * 8;
    player.dir_h = dir;
    player.dir_v = 0;

    test_keys = KBIT_DOWN;
    game_frame();
    test_keys = 0;

    if (player.state != ST_LADDER)
        return 255;
    return player.x;
}

/* 46-54: a vertical request snaps onto a ladder within CLIMB_SNAP pixels */
static void test_climb_snap(void)
{
    /* Dead on the ladder column works, and does not move the player. */
    check(climb_up_from(232, 1) == 232);

    /* Two pixels either side snap onto the ladder. */
    check(climb_up_from(230, 1) == 232);
    check(climb_up_from(234, 1) == 232);

    /* Half a cell out is a tie, broken the way the player is walking: into
     * the ladder when approaching it, past it when leaving. */
    check(climb_up_from(228, 1) == 232);
    check(climb_up_from(236, -1) == 232);
    check(climb_up_from(236, 1) == 255);

    /* A whole cell away is still a miss, so this has not become
     * "climb from anywhere". */
    check(climb_up_from(222, 1) == 255);

    /* Stepping down snaps the same way. */
    check(climb_down_from(230, 1) == 232);
    check(climb_down_from(236, -1) == 232);
}

/* Mirrors game.c's obj_px/obj_px2 pair: the two columns an actor occupies. */
static uint8_t cell_solid(uint8_t o)
{
    if (o == OBJ_BRICKS)    return 1;
    if (o == OBJ_SOLID)     return 1;
    if (o == OBJ_HIDBRICKS) return 1;
    return 0;
}

static uint8_t embedded(uint8_t x, uint8_t y)
{
    if (cell_solid(room_obj((uint8_t)(x >> 3), (uint8_t)(y >> 3))))
        return 1;
    if ((x & 7) != 0) {
        if (cell_solid(room_obj((uint8_t)((x >> 3) + 1), (uint8_t)(y >> 3))))
            return 1;
    }
    return 0;
}

/* 55-56: a vertical request must never leave the player standing in solid
 * terrain. The snap moves the player sideways onto a column boundary, and
 * the cell *below* that column being open says nothing about the column
 * itself -- a wall with a gap under it would swallow the player, who then
 * finds blocked_h refusing every move. Sweeping every stopping position in
 * the training room is cheap and does not depend on me guessing the one
 * layout that triggers it. */
static uint8_t sweep_embeds(uint8_t key)
{
    uint8_t row, x, bad;

    bad = 0;
    for (row = 1; row < (ROOM_ROWS - 1); row++) {
        /* The room cannot change during the sweep: no guards run and no dig
         * key is pressed, so one unpack per row is enough. Unpacking per
         * position made this far too slow to finish under MAME. */
        game_start_room(ROOM_TRAIN);
        enemy_count = 0;

        for (x = 0; x < 248; x += 2) {
            /* Only start from positions the player could really reach. */
            if (embedded(x, (uint8_t)(row * 8)))
                continue;

            player.state = ST_WALKING;
            player.x     = x;
            player.y     = (uint8_t)(row * 8);
            player.dir_h = 1;
            player.dir_v = 0;

            test_keys = key;
            game_frame();
            test_keys = 0;

            if (embedded(player.x, player.y))
                bad++;
        }
    }
    return bad;
}

static void test_snap_never_embeds(void)
{
    check(sweep_embeds(KBIT_UP) == 0);
    check(sweep_embeds(KBIT_DOWN) == 0);
}

/* 57: a guard's occupancy marker must not be snapped into. Guards write a
 * solid marker into the terrain grid, so a guard can step into a column the
 * player is merely straddling -- blocked_h never gets a chance to refuse it,
 * because the player was already there. A down request must not then snap the
 * player into that cell, which would wedge them in terrain where every later
 * move is refused. The gap under the marker is what makes it look passable. */
static void test_snap_rejects_guard_marker(void)
{
    game_start_room(ROOM_TRAIN);
    enemy_count = 0;

    room_set_cell(9, 5, OBJ_EMPTY);    /* the player's own cell, open     */
    room_set_cell(9, 6, OBJ_BRICKS);   /* floor under the player          */
    room_set_cell(10, 5, OBJ_SOLID);   /* a guard marker beside them      */
    room_set_cell(10, 6, OBJ_EMPTY);   /* open below it: the old trap     */

    player.state = ST_WALKING;
    player.x     = 9 * 8 + 6;          /* off 6, so the snap targets col 10 */
    player.y     = 5 * 8;
    player.dir_h = 1;
    player.dir_v = 0;

    test_keys = KBIT_DOWN;
    game_frame();
    test_keys = 0;

    check(player.x == (9 * 8 + 6));
}

/* Stand the player at pixel x on a brick floor with diggable bricks either
 * side, facing dir, and tap dig. Returns the player's x once a brick has
 * gone, or 255 if no dig ever started. Cols 7..12 of row 6 are bricks, so
 * either direction has something to dig. */
static uint8_t dig_snap_from(uint8_t x, int8_t dir)
{
    uint8_t i, c;

    game_start_room(ROOM_TRAIN);
    enemy_count = 0;

    for (c = 7; c <= 12; c++) {
        room_set_cell(c, 5, OBJ_EMPTY);
        room_set_cell(c, 6, OBJ_BRICKS);
    }

    player.state = ST_WALKING;
    player.x     = x;
    player.y     = 5 * 8;
    player.dir_h = dir;
    player.dir_v = 0;

    test_keys = 0;
    game_frame();               /* settle the edge detector */
    test_keys = KBIT_DIG;

    for (i = 0; i < 60; i++) {
        game_frame();
        test_keys = 0;          /* digging is edge triggered */
        for (c = 7; c <= 12; c++) {
            if (room_obj(c, 6) != OBJ_BRICKS)
                return player.x;
        }
    }
    return 255;
}

/* 58-63: a dig request snaps onto the nearest column. The original digs only
 * from an exactly aligned column, which swallows three presses in four. */
static void test_dig_snap(void)
{
    /* Dead on a column digs and does not move the player. */
    check(dig_snap_from(9 * 8, 1) == 9 * 8);

    /* Two pixels either side snap back onto column 9. */
    check(dig_snap_from(9 * 8 + 2, 1) == 9 * 8);
    check(dig_snap_from(9 * 8 - 2, 1) == 9 * 8);

    /* Half a cell out is a tie, broken the way the dig faces. */
    check(dig_snap_from(9 * 8 + 4, 1) == 10 * 8);
    check(dig_snap_from(9 * 8 + 4, -1) == 9 * 8);

    /* The snap never leaves the player standing inside terrain. */
    check(!embedded(player.x, player.y));
}

/* Put the player on a ladder at pixel y and ask to step off sideways.
 * Returns the resulting y, or 255 if no step-off happened. */
static uint8_t ladder_exit_from(uint8_t y, int8_t in_h, int8_t dir_v)
{
    game_start_room(ROOM_TRAIN);
    enemy_count = 0;

    /* A ladder in column 9 over rows 5-6, open ground either side of row 5. */
    room_set_cell(9, 5, OBJ_LADDER);
    room_set_cell(9, 6, OBJ_LADDER);
    room_set_cell(8, 5, OBJ_EMPTY);
    room_set_cell(10, 5, OBJ_EMPTY);
    room_set_cell(8, 6, OBJ_BRICKS);
    room_set_cell(10, 6, OBJ_BRICKS);

    player.state = ST_LADDER;
    player.x     = 9 * 8;
    player.y     = y;
    player.dir_h = 0;
    player.dir_v = dir_v;

    test_keys = (in_h < 0) ? KBIT_LEFT : KBIT_RIGHT;
    game_frame();
    test_keys = 0;

    if (player.state != ST_WALKING)
        return 255;
    return player.y;
}

/* 64-68: stepping off a ladder snaps to the nearest rung, for the same
 * reason climbing on does -- the player climbs 2 px at a time. */
static void test_ladder_exit_snap(void)
{
    /* Dead on a rung steps off and does not move the player. */
    check(ladder_exit_from(5 * 8, 1, -1) == 5 * 8);

    /* Two pixels either side of the rung snap onto it. */
    check(ladder_exit_from(5 * 8 + 2, 1, -1) == 5 * 8);
    check(ladder_exit_from(5 * 8 - 2, 1, -1) == 5 * 8);

    /* A half-cell tie goes the way the player is climbing: up to row 5,
     * which is open, or down to row 6, which is walled in either side. */
    check(ladder_exit_from(5 * 8 + 4, 1, -1) == 5 * 8);
    check(ladder_exit_from(5 * 8 + 4, 1, 1) == 255);
}

int main(void)
{
    test_count = 0;
    test_fail  = 0;
    test_done  = 0;

    test_catch();
    test_trap_and_escape();
    test_crush();
    test_guard_gold();
    test_respawn_returns_gold();
    test_occupancy_balanced();
    test_level_completion();
    test_level_bonus();
    test_player_gold();
    test_five_guards();
    test_cheat();
    test_training_room();
    test_climb_snap();
    test_snap_never_embeds();
    test_snap_rejects_guard_marker();
    test_dig_snap();
    test_ladder_exit_snap();

    test_done = 1;

    /* Park here so the results stay put while the driver reads them. An
     * empty loop body makes sccz80 emit dangling labels, so keep the
     * assignment inside it. */
    while (1) {
        test_done = 1;
    }
}
