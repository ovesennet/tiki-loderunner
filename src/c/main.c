/* TIKI LODE RUNNER - main.c
 * Program shell: set up the platform, run the title screen, the game and the
 * room viewer, and restore the machine on exit.
 *
 * Rooms render in Tiki mode 3 with per-pixel colour. A game starts at
 * room 0, the training level added by this port, and continues into the
 * original's room 1.
 *
 * Keys are this port's own left-hand cluster: A/D move left and right,
 * I/J climb and descend, SPACE (the Tiki's MLMROM key) digs and P pauses.
 * In the viewer, ',' and '.' step through rooms, the dig key
 * toggles the editor render context, and RETURN or BRYT returns to the
 * title screen.
 *
 * The title screen lists only RETURN and BRYT. The cheat (0) and the room
 * viewer (.) remain fully active but are intentionally unadvertised.
 */

#include "defs.h"
#include "video.h"
#include "input.h"
#include "room.h"
#include "game.h"
#include "sound.h"

static Spawn   s_spawn;
static uint8_t s_room = ROOM_TRAIN;
static uint8_t s_context = RENDER_GAME;

static void show_room(void)
{
    room_unpack(s_room);
    room_spawn(s_room, &s_spawn);
    room_present(s_context);
    spawn_draw(&s_spawn);
    hud_draw(s_room, 5, "0000000");
}

static void title(void)
{
    vid_clear();

    /* Centred on the 256 px logical screen: x = (256 - 6 * length) / 2,
     * the font advancing 6 px per character. The two key/action lists are
     * centred as blocks on their widest line so the columns stay aligned. */
    vid_draw_text(98, 28, "BRODERBUND", COL_BRBLUE);
    vid_draw_text(95, 44, "LODE RUNNER", COL_BRYELLOW);
    vid_draw_text(92, 60, "FOR TIKI-100", COL_BRCYAN);
    vid_draw_text(77, 76, "ARCTIC RETRO 2026", COL_BRRED);

    /* Control list centred as a block on its widest line, "SPACE DIG P
     * PAUSE" at 17 characters: x = (256 - 6 * 17) / 2. The left column is
     * padded to 10 characters so the right column stays aligned. */
    vid_draw_text(77, 116, "A LEFT    D RIGHT", COL_BRWHITE);
    vid_draw_text(77, 128, "I UP      J DOWN", COL_BRWHITE);
    vid_draw_text(77, 140, "SPACE DIG P PAUSE", COL_BRWHITE);

    /* Centred as a block on its widest line, "BRYT     QUIT TO CPM" at 20
     * characters: x = (256 - 6 * 20) / 2.
     *
     * The cheat (0 = play without dying) and the room viewer (.) are
     * deliberately NOT listed. Both keys still work -- see main() -- they
     * are just not advertised. */
    vid_draw_text(68, 168, "RETURN   PLAY", COL_WHITE);
    vid_draw_text(68, 180, "BRYT     QUIT TO CPM", COL_WHITE);

    vid_draw_text(92, 236, "PRESS RETURN", COL_BRYELLOW);
}

/* Room browser for checking the static renderer. */
static void room_viewer(void)
{
    vid_clear();
    show_room();
    input_flush();

    for (;;) {
        input_poll();

        /* BRYT is the documented exit, but MAME binds Esc to its own UI, so
         * RETURN leaves the viewer too. Sequential ifs, not ||, for sccz80. */
        if (input_shell_pressed(SBIT_QUIT)) return;
        if (input_pressed(KBIT_SELECT)) return;

        if (input_shell_pressed(SBIT_NEXT)) {
            s_room = (s_room == ROOM_COUNT) ? ROOM_TRAIN : (uint8_t)(s_room + 1);
            show_room();
        } else if (input_shell_pressed(SBIT_PREV)) {
            s_room = (s_room == ROOM_TRAIN) ? ROOM_COUNT : (uint8_t)(s_room - 1);
            show_room();
        } else if (input_pressed(KBIT_DIG)) {
            s_context = (s_context == RENDER_GAME) ? RENDER_EDITOR
                                                   : RENDER_GAME;
            show_room();
        }

        wait_vsync();
    }
}

/* Shown when the last life is lost. Waits for RETURN or the quit key. */
static void game_over(void)
{
    uint8_t i;

    vid_draw_text(104, 112, "GAME OVER", COL_BRRED);
    vid_draw_text(80, 132, "PRESS RETURN", COL_BRWHITE);

    /* Ignore whatever was held down at the moment of death. */
    for (i = 0; i < 25; i++)
        wait_vsync();
    input_flush();

    for (;;) {
        input_poll();
        if (input_pressed(KBIT_SELECT)) return;
        if (input_shell_pressed(SBIT_QUIT)) return;
        wait_vsync();
    }
}

static void play(uint8_t cheat)
{
    uint8_t result;

    /* Room 0 is the training level, played once before the original's
     * room 1. Finishing the last room wraps back to room 1, not to
     * training. */
    s_room = ROOM_TRAIN;
    score  = 0;
    game_set_cheat(cheat);
    game_set_lives(START_LIVES);

    for (;;) {
        vid_clear();
        game_start_room(s_room);
        input_flush();

        for (;;) {
            result = game_frame();
            if (result != PLAY_RUNNING) break;
        }

        if (result == PLAY_QUIT)
            return;

        if (result == PLAY_COMPLETE) {
            game_level_bonus();
            if (s_room == ROOM_COUNT) {
                snd_victory();
                s_room = 0;
            }
            s_room = (uint8_t)(s_room + 1);
            continue;
        }

        /* PLAY_DIED: lose a life and restart the room, or end the game. */
        snd_caught();
        if (game_lives() <= 1) {
            game_set_lives(0);
            game_over();
            return;
        }
        game_set_lives((uint8_t)(game_lives() - 1));
    }
}

int main(void)
{
    vid_init();
    input_init();
    snd_init();

    for (;;) {
        title();
        input_flush();

        for (;;) {
            input_poll();
            if (input_pressed(KBIT_SELECT)) {
                play(0);
                break;
            }
            if (input_shell_pressed(SBIT_CHEAT)) {
                play(1);
                break;
            }
            if (input_shell_pressed(SBIT_NEXT)) {
                room_viewer();
                break;
            }
            if (input_shell_pressed(SBIT_QUIT))
                goto done;
            wait_vsync();
        }
    }

done:
    snd_off();
    input_shutdown();
    vid_shutdown();
    return 0;
}
