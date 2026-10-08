/* Sound test.
 *
 * Plays each effect separated by silence, so a recording of MAME's AY
 * output can be segmented and measured. This checks the things that
 * cannot be checked by reading memory: that the PSG is reachable on
 * ports $16/$17 at all, that the pitch conversion lands on the right
 * frequency for a 2 MHz AY, and that the cycle-counted millisecond
 * delay really is a millisecond on a 4 MHz Z80.
 *
 * It deliberately does not link the game, so a sound failure here
 * cannot be confused with a gameplay one.
 */

#include <stdint.h>
#include "../src/c/sound.h"

/* sound.asm */
extern uint16_t snd_ms;
extern void snd_delay_asm(void);
extern void snd_off_asm(void);

/* Set once the script has finished, so the runner can tell a completed
 * run from one that hung or never loaded. */
uint8_t test_done;

static void gap(uint16_t ms)
{
    snd_off_asm();
    snd_ms = ms;
    snd_delay_asm();
}

int main(void)
{
    test_done = 0;
    snd_init();

    /* Lead-in, so the floppy noise of loading is well clear of the
     * first tone. */
    gap(1000);

    /* A single long steady tone: 294 -> 425.2 Hz for 238 ms. This one
     * measures both the pitch conversion and the delay calibration. */
    snd_tally();
    gap(400);

    /* Four distinct notes with a rest in the middle. */
    snd_victory();
    gap(400);

    /* The four dig stages, as a short falling run. Unrolled: sccz80
     * miscompiles a for-loop here. */
    snd_dig(0);
    snd_dig(1);
    snd_dig(2);
    snd_dig(3);
    gap(400);

    snd_gold();
    gap(400);

    /* The long falling death sweep. */
    snd_caught();
    gap(400);

    snd_off();

    /* The loop needs a body: sccz80's optimiser turns an empty
     * for(;;){} into a pair of labels defined as each other, which the
     * assembler then rejects as undefined. */
    for (;;)
        test_done = 1;
}
