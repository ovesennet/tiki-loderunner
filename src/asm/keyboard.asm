; TIKI LODE RUNNER - keyboard.asm
; Direct keyboard matrix scanning via port $00.
;
; TIKI-100 keyboard: 8 rows x 12 columns on port $00.
;   - Write to port $00 to reset the scan to column 1
;   - Each read of port $00 returns the next column (1..12)
;   - Bit SET = key NOT pressed, bit CLEAR = key pressed
;
; The whole matrix is sampled in one pass and every action is decoded from
; that snapshot, so a frame can never see two different keyboard states.
;
; Keyboard only - no joystick. The movement keys are a WASD-style left-hand
; cluster chosen for this port rather than the Spectrum's originals, with the
; two vertical keys on the right hand.
;
;   A = left    col 2 bit 3       RETUR  = select   col 1 bit 3
;   D = right   col 3 bit 6       MLMROM = dig      col 1 bit 4
;   I = up      col 6 bit 1       BRYT   = quit     col 1 bit 2
;   J = down    col 5 bit 6       ,      = prev     col 6 bit 3
;   P = pause   col 7 bit 1       .      = next     col 6 bit 7
;                                 0      = cheat    col 7 bit 0
;
; MLMROM is MELLOMROM, the Norwegian space bar, and is the only dig key.
; Space therefore does not select -- RETUR alone starts a game, which is what
; the title screen advertises.

    SECTION code_driver

    PUBLIC  _kbd_init
    PUBLIC  _kbd_shutdown
    PUBLIC  _kbd_scan

; Low byte (gameplay actions)
KBIT_LEFT   equ 0
KBIT_RIGHT  equ 1
KBIT_UP     equ 2
KBIT_DOWN   equ 3
KBIT_DIG    equ 4
KBIT_PAUSE  equ 5
KBIT_SELECT equ 6
KBIT_ANY    equ 7

; High byte (shell / browser keys)
KBIT_QUIT   equ 0
KBIT_PREV   equ 1
KBIT_NEXT   equ 2
KBIT_CHEAT  equ 3

; ------------------------------------------------
; uint16_t kbd_scan(void)
; Returns L = action bitmask, H = shell bitmask.
; ------------------------------------------------
_kbd_init:
    ret

_kbd_shutdown:
    ret

_kbd_scan:
    ld      e, 0                ; action bitmask
    ld      d, 0                ; shell bitmask
    ld      b, 0                ; any-key accumulator

    out     ($00), a            ; reset scan to column 1

    ; --- Column 1: BRYT, RETUR, MLMROM ---
    in      a, ($00)
    ld      c, a
    cpl
    or      b
    ld      b, a
    bit     2, c
    jr      nz, ks_no_bryt
    set     KBIT_QUIT, d
ks_no_bryt:
    bit     3, c
    jr      nz, ks_no_retur
    set     KBIT_SELECT, e
ks_no_retur:
    bit     4, c
    jr      nz, ks_no_space
    set     KBIT_DIG, e
ks_no_space:

    ; --- Column 2: A (left) ---
    in      a, ($00)
    ld      c, a
    cpl
    or      b
    ld      b, a
    bit     3, c
    jr      nz, ks_no_a
    set     KBIT_LEFT, e
ks_no_a:

    ; --- Column 3: D (right) ---
    in      a, ($00)
    ld      c, a
    cpl
    or      b
    ld      b, a
    bit     6, c
    jr      nz, ks_no_d
    set     KBIT_RIGHT, e
ks_no_d:

    ; --- Column 4 (accumulate any-key only) ---
    in      a, ($00)
    cpl
    or      b
    ld      b, a

    ; --- Column 5: J (down) ---
    in      a, ($00)
    ld      c, a
    cpl
    or      b
    ld      b, a
    bit     6, c
    jr      nz, ks_no_j
    set     KBIT_DOWN, e
ks_no_j:

    ; --- Column 6: I (up), ',' (prev), '.' (next) ---
    in      a, ($00)
    ld      c, a
    cpl
    or      b
    ld      b, a
    bit     1, c
    jr      nz, ks_no_i
    set     KBIT_UP, e
ks_no_i:
    bit     3, c
    jr      nz, ks_no_comma
    set     KBIT_PREV, d
ks_no_comma:
    bit     7, c
    jr      nz, ks_no_dot
    set     KBIT_NEXT, d
ks_no_dot:

    ; --- Column 7: '0' (cheat), P (pause) ---
    in      a, ($00)
    ld      c, a
    cpl
    or      b
    ld      b, a
    bit     0, c
    jr      nz, ks_no_zero
    set     KBIT_CHEAT, d
ks_no_zero:
    bit     1, c
    jr      nz, ks_no_p
    set     KBIT_PAUSE, e
ks_no_p:

    ; --- Columns 8-12 (accumulate any-key only) ---
    in      a, ($00)
    cpl
    or      b
    ld      b, a
    in      a, ($00)
    cpl
    or      b
    ld      b, a
    in      a, ($00)
    cpl
    or      b
    ld      b, a
    in      a, ($00)
    cpl
    or      b
    ld      b, a
    in      a, ($00)
    cpl
    or      b
    ld      b, a

    ld      a, b
    or      a
    jr      z, ks_no_any
    set     KBIT_ANY, e
ks_no_any:

    ld      l, e
    ld      h, d
    ret
