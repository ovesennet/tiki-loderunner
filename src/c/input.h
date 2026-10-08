/* TIKI LODE RUNNER - input.h
 * Direct keyboard matrix scanning. The key assignments are this port's own;
 * see keyboard.asm for the Tiki matrix positions.
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Low byte of kbd_scan() - gameplay actions */
#define KBIT_LEFT    0x01   /* A */
#define KBIT_RIGHT   0x02   /* D */
#define KBIT_UP      0x04   /* I */
#define KBIT_DOWN    0x08   /* J */
#define KBIT_DIG     0x10   /* MLMROM (space) */
#define KBIT_PAUSE   0x20   /* P */
#define KBIT_SELECT  0x40   /* RETUR or MLMROM */
#define KBIT_ANY     0x80

/* High byte of kbd_scan() - shell keys */
#define SBIT_QUIT    0x01   /* BRYT */
#define SBIT_PREV    0x02   /* , */
#define SBIT_NEXT    0x04   /* . */
#define SBIT_CHEAT   0x08   /* 0 - starts an invulnerable game */

void input_init(void);
void input_shutdown(void);

/* Sample the whole matrix once; all actions decode from that snapshot. */
void input_poll(void);
void input_flush(void);

uint8_t input_held(uint8_t bit);
uint8_t input_pressed(uint8_t bit);       /* edge, low byte  */
uint8_t input_shell_pressed(uint8_t bit); /* edge, high byte */
uint8_t input_any(void);

#endif /* INPUT_H */
