; ============================================================
; sound.asm - AY-3-8912 driver for the Tiki-100
;
; The Tiki-100 wires an AY-3-8912A PSG at register-select port $16 and
; data port $17, clocked at 2 MHz independently of the 4 MHz CPU.
;
; TWO HARD RULES, both of which will visibly corrupt the display if
; broken:
;
;   1. Never write AY register 14. On the Tiki that register is the
;      PSG's I/O port A, and port A drives the video scroll latch.
;      z88dk's own gr_vscroll() scrolls the screen by writing it.
;   2. Never clear the whole register file. z88dk's psg_init() does
;      exactly that, which is why this driver exists instead.
;
; For the same reason register 7 is only ever written as $7E or $7F.
; Both keep bit 6 set, which holds port A in output mode; clearing it
; would float the scroll latch.
;
; Only channel A is used. The original ZX Spectrum code drives the
; 1-bit beeper, so a single square-wave voice is the faithful mapping.
;
; C communicates through the parameter block below, matching the
; convention already used by screen.asm rather than the stack.
; ============================================================

    SECTION code_driver

    PUBLIC  _snd_tone_asm
    PUBLIC  _snd_off_asm
    PUBLIC  _snd_delay_asm

; ============================================================
; Parameter block for C -> ASM communication
; ============================================================
    SECTION bss_graphics

    PUBLIC  _snd_period
    PUBLIC  _snd_vol
    PUBLIC  _snd_ms

_snd_period:    defw 0      ; 12-bit AY tone period, 1..4095
_snd_vol:       defb 0      ; 0..15
_snd_ms:        defw 0      ; delay length in milliseconds

    SECTION code_driver

; ------------------------------------------------------------
; ay_set - write one AY register
;   In:  A = register number, E = value
;   Uses: A
;
; The select/data pair must not be split by an interrupt that also
; touches the PSG, so it runs with interrupts masked. Callers always
; come from the main loop with interrupts enabled, which is why this
; ends with a plain ei.
; ------------------------------------------------------------
ay_set:
    di
    out     ($16), a            ; latch register number
    ld      a, e
    out     ($17), a            ; write value
    ei
    ret

; ------------------------------------------------------------
; snd_tone_asm - start a tone on channel A
;   In:  _snd_period, _snd_vol
;
; The period is programmed before the channel is unmuted so the tone
; never sounds briefly at the previous pitch.
; ------------------------------------------------------------
_snd_tone_asm:
    ld      hl, (_snd_period)

    ld      e, l                ; R0 = period, fine
    xor     a
    call    ay_set

    ld      a, h                ; R1 = period, coarse (4 bits only)
    and     $0F
    ld      e, a
    ld      a, 1
    call    ay_set

    ld      e, $7E              ; R7 = tone A on, B/C and all noise off,
    ld      a, 7                ;      port A still an output
    call    ay_set

    ld      a, (_snd_vol)       ; R8 = channel A amplitude, fixed level
    and     $0F
    ld      e, a
    ld      a, 8
    call    ay_set

    ret

; ------------------------------------------------------------
; snd_off_asm - silence channel A
;
; Volume is dropped before the mixer is closed, so no click leaks out
; between the two writes.
; ------------------------------------------------------------
_snd_off_asm:
    ld      e, 0
    ld      a, 8
    call    ay_set

    ld      e, $7F              ; all tones off, port A still an output
    ld      a, 7
    call    ay_set

    ret

; ------------------------------------------------------------
; snd_delay_asm - busy-wait for _snd_ms milliseconds
;
; Cycle-counted for the Tiki's 4 MHz Z80, where 1 ms is 4000 T-states.
; One pass of the inner loop is 9 nops plus djnz: 9*4 + 13 = 49 T.
; With B = 81 the loop body is 3969 T, and the surrounding
; ld b / dec de / or / jr adds 7 + 6 + 4 + 4 + 12 T, less the 5 T the
; final djnz saves by falling through. That totals 3997 T per
; millisecond, 0.08% fast.
;
; Only registers are touched, deliberately: anything that went through
; memory would pick up video-RAM wait states and make the timing
; depend on where the code happens to live.
;
; Interrupts stay enabled, so firmware ISR time is added on top. That
; matches the original, whose BEEPER loops were likewise interruptible.
; ------------------------------------------------------------
_snd_delay_asm:
    ld      de, (_snd_ms)
    ld      a, d
    or      e
    ret     z

snd_dly_ms:
    ld      b, 81
snd_dly_inner:
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    djnz    snd_dly_inner

    dec     de
    ld      a, d
    or      e
    jr      nz, snd_dly_ms
    ret
