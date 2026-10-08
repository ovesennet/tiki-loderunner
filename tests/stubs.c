/* Test-harness stubs.
 *
 * game.c and room.c touch the machine only through video.h and input.h, so
 * linking them against these stubs produces a test program that exercises
 * the real game loop with no screen output and fully scripted input.
 *
 * The tests are built for the Tiki with the same sccz80 toolchain as the
 * game, which matters: they then cover the compiler's code generation as
 * well as the logic. They exist because the guard rules are impractical to
 * stage in the emulator by hand -- three guards converge on a standing
 * player in about two seconds, and a death restarts the room mid-script.
 */

#include "../src/c/video.h"
#include "../src/c/input.h"
#include "../src/c/room.h"

/* ---- video: record nothing, draw nothing ---- */

void vid_init(void) {}
void vid_shutdown(void) {}
void vid_clear(void) {}
void vid_fill_rect(uint16_t x, uint16_t y, uint8_t w, uint8_t h, uint8_t colour)
{
    (void)x; (void)y; (void)w; (void)h; (void)colour;
}
void vid_draw_text(uint16_t x, uint16_t y, const char *str, uint8_t colour)
{
    (void)x; (void)y; (void)str; (void)colour;
}
void wait_vsync(void) {}

/* The compositor is exercised for its side effects on the visual layer in
 * the real build; here only the logical state matters. */
void cell_draw(uint8_t col, uint8_t row, uint8_t glyph, uint8_t ink, uint8_t paper)
{
    (void)col; (void)row; (void)glyph; (void)ink; (void)paper;
}
void cell_fill_row(uint8_t row, uint8_t colour) { (void)row; (void)colour; }
void sprite_draw(uint8_t x, uint8_t y, uint8_t glyph, uint8_t ink)
{
    (void)x; (void)y; (void)glyph; (void)ink;
}
uint8_t obj_colour(uint8_t obj) { (void)obj; return 0; }

/* ---- input: driven by the test ---- */

uint8_t test_keys;          /* currently held KBIT_* mask      */
static uint8_t s_sampled;   /* what input_poll latched         */
static uint8_t s_prev;      /* previous latch, for edges       */

void input_init(void) {}
void input_shutdown(void) {}

void input_poll(void)
{
    s_prev = s_sampled;
    s_sampled = test_keys;
}

void input_flush(void)
{
    s_prev = s_sampled = test_keys;
}

uint8_t input_held(uint8_t bit) { return (uint8_t)(s_sampled & bit); }

uint8_t input_pressed(uint8_t bit)
{
    if ((s_sampled & bit) == 0)
        return 0;
    if ((s_prev & bit) != 0)
        return 0;
    return bit;
}

uint8_t input_shell_pressed(uint8_t bit) { (void)bit; return 0; }

uint8_t input_any(void) { return s_sampled; }

/* ---- sound: silent and, crucially, instant ----
 *
 * The real effects block for the original beeper's duration, which would
 * add minutes to a run of the suite without testing anything. The gameplay
 * logic never reads anything back from them.
 */

#include "../src/c/sound.h"

uint8_t snd_enabled = 0;

void snd_init(void) {}
void snd_off(void) {}
void snd_gold(void) {}
void snd_dig(uint8_t stage) { (void)stage; }
void snd_caught(void) {}
void snd_exit_step(uint8_t step) { (void)step; }
void snd_victory(void) {}
void snd_tally(void) {}
