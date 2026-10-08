/* ============================================================
 * sound.h - beeper-equivalent sound effects
 *
 * Every effect here is a conversion of one ZX Spectrum BEEPER call in
 * the original. BEEPER is blocking: the game stops while the tone
 * plays, and the tone's length is part of the game's feel. These
 * functions block for the same length of time for that reason, so
 * they must only be called from the main game loop.
 * ============================================================ */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Set to 0 to mute every effect. */
extern uint8_t snd_enabled;

void snd_init(void);
void snd_off(void);

void snd_gold(void);              /* gold picked up */
void snd_dig(uint8_t stage);      /* dig animation, stage 0..3 */
void snd_caught(void);            /* caught by a guard, falling sweep */
void snd_exit_step(uint8_t step); /* one of 30 end-of-level bonus steps */
void snd_victory(void);           /* game completed */
void snd_tally(void);             /* one bonus-per-life tally beep */

#endif /* SOUND_H */
