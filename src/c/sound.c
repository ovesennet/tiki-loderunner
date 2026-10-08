/* ============================================================
 * sound.c - ZX Spectrum BEEPER effects remapped onto the AY-3-8912
 *
 * The original drives the Spectrum's 1-bit beeper through a routine
 * taking HL = pitch and DE = cycle count. Measured against that
 * routine's inner loop, the tone it produces is
 *
 *     f = 437500 / (HL + 30.125)  Hz, lasting (DE + 1) / f seconds.
 *
 * The Tiki's AY runs at 2 MHz and divides by 16, so f = 2000000 /
 * (16 * period). Equating the two gives the conversions below. Both
 * are kept as integer expressions: there is no FPU, and sccz80's
 * floating point would be far too slow to sit inside a sweep.
 *
 * Every effect blocks for the original's duration. That is not an
 * oversight - on the Spectrum the beeper routine holds the CPU, so
 * the pauses it creates are part of the game's rhythm. The caught
 * sweep in particular is a deliberate ~2.9 second death pause.
 * ============================================================ */

#include <stdint.h>
#include "sound.h"

/* sound.asm */
extern void snd_tone_asm(void);
extern void snd_off_asm(void);
extern void snd_delay_asm(void);

extern uint16_t snd_period;
extern uint8_t  snd_vol;
extern uint16_t snd_ms;

#define SND_VOL 12

uint8_t snd_enabled = 1;

/* ------------------------------------------------------------
 * Conversions from the original's (HL, DE) beeper arguments
 * ------------------------------------------------------------ */

/* AY tone period for a given beeper pitch. The register is 12 bits,
 * so the result is clamped rather than allowed to wrap - a wrapped
 * period would jump to a high note in the middle of a falling sweep.
 *
 * Kept to 16-bit arithmetic: this runs once per sweep step, and
 * sccz80's 32-bit divide is slow enough to stretch a sweep audibly.
 * hl * 2 is safe because the largest pitch any effect asks for is
 * 12544, from the bottom of the death sweep. */
static uint16_t ay_period(uint16_t hl)
{
    uint16_t p;

    if (hl > 32000U)
        hl = 32000U;
    p = (uint16_t)((hl * 2U + 60U) / 7U);
    if (p < 1U)
        p = 1U;
    if (p > 4095U)
        p = 4095U;
    return p;
}

/* Duration in milliseconds of a beeper call. */
static uint16_t ay_ms(uint16_t hl, uint16_t de)
{
    return (uint16_t)((((uint32_t)de + 1UL) * ((uint32_t)hl * 2UL + 60UL))
                      / 875UL);
}

/* Play one tone and hold for its duration. The channel is left
 * sounding so sweeps can retune without a gap between steps. */
static void tone(uint16_t hl, uint16_t de)
{
    snd_period = ay_period(hl);
    snd_vol    = SND_VOL;
    snd_tone_asm();

    snd_ms = ay_ms(hl, de);
    snd_delay_asm();
}

static void silence_ms(uint16_t ms)
{
    snd_off_asm();
    snd_ms = ms;
    snd_delay_asm();
}

/* ------------------------------------------------------------
 * Effects
 * ------------------------------------------------------------ */

void snd_init(void)
{
    snd_off_asm();
}

void snd_off(void)
{
    snd_off_asm();
}

/* Gold pickup: a single short blip, HL=1000 DE=7. */
void snd_gold(void)
{
    if (snd_enabled == 0)
        return;
    tone(1000, 7);
    snd_off_asm();
}

/* Dig: one tone per animation frame. The original builds HL as
 * H = (frame + 1) * 2 + 4 while leaving L at $32, which it inherits
 * from the address of the frame counter rather than setting it - a
 * quirk, but an audible one, so it is reproduced exactly. */
void snd_dig(uint8_t stage)
{
    uint16_t hl;

    if (snd_enabled == 0)
        return;
    if (stage > 3)
        return;

    hl = (uint16_t)((uint16_t)(((uint16_t)stage + 1) * 2 + 4) << 8) | 0x32;
    tone(hl, 2);
    snd_off_asm();
}

/* Caught by a guard: 50 steps descending from the top of the range,
 * HL = step * 256. Each step is brief at the start and lengthens as
 * the pitch falls, because the beeper's cycle count is fixed. */
void snd_caught(void)
{
    uint8_t i;

    if (snd_enabled == 0)
        return;

    for (i = 0; i < 50; i++)
        tone((uint16_t)((uint16_t)i << 8), 3);

    snd_off_asm();
}

/* End-of-level bonus: 30 steps, called one per bonus award so the
 * sweep and the score tally stay in step as the original's do.
 * step counts 0..29 while the original's B counts 30 down to 1. */
void snd_exit_step(uint8_t step)
{
    uint8_t b;

    if (snd_enabled == 0)
        return;
    if (step > 29)
        return;

    b = (uint8_t)(30 - step);
    tone((uint16_t)(((uint16_t)(b >> 1) << 8) | 0x80), 3);

    if (step == 29)
        snd_off_asm();
}

/* Victory jingle. The rest in the middle is five frames of silence. */
static const uint16_t s_vic_hl[] = { 0x086B, 0x067C, 0x04B8, 0x042B,
                                     0x0000, 0x04B8, 0x042B };
static const uint16_t s_vic_de[] = { 29, 36, 50, 55, 0, 39, 110 };

void snd_victory(void)
{
    uint8_t i;

    if (snd_enabled == 0)
        return;

    for (i = 0; i < 7; i++) {
        if (s_vic_de[i] == 0) {
            silence_ms(100);
            continue;
        }
        tone(s_vic_hl[i], s_vic_de[i]);
    }

    snd_off_asm();
}

/* One beep per life converted to bonus points at the end of the game. */
void snd_tally(void)
{
    if (snd_enabled == 0)
        return;
    tone(1000, 100);
    snd_off_asm();
}
