/* TIKI LODE RUNNER - input.c
 * Edge/held decoding on top of a single whole-matrix sample per poll.
 */

#include "input.h"

extern uint16_t kbd_scan(void);
extern void kbd_init(void);
extern void kbd_shutdown(void);

static uint16_t s_cur;
static uint16_t s_prev;

void input_init(void)
{
    kbd_init();
    s_cur = 0;
    s_prev = 0;
}

void input_shutdown(void)
{
    kbd_shutdown();
}

void input_flush(void)
{
    s_cur = 0;
    s_prev = 0;
}

void input_poll(void)
{
    s_prev = s_cur;
    s_cur = kbd_scan();
}

uint8_t input_held(uint8_t bit)
{
    /* These predicates avoid `expr ? 1 : 0`: sccz80 compiles the conditional
     * operator over a logical/bitwise test by re-testing a stale carry flag,
     * which inverts the result. Plain `if` statements compile correctly. */
    if ((uint8_t)s_cur & bit)
        return 1;
    return 0;
}

uint8_t input_pressed(uint8_t bit)
{
    if (((uint8_t)s_cur & bit) == 0)
        return 0;
    if ((uint8_t)s_prev & bit)
        return 0;
    return 1;
}

uint8_t input_shell_pressed(uint8_t bit)
{
    uint8_t cur  = (uint8_t)(s_cur >> 8);
    uint8_t prev = (uint8_t)(s_prev >> 8);

    if ((cur & bit) == 0)
        return 0;
    if (prev & bit)
        return 0;
    return 1;
}

uint8_t input_any(void)
{
    if ((uint8_t)s_cur & KBIT_ANY)
        return 1;
    return 0;
}
