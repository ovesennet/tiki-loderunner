; TIKI LODE RUNNER - sprite.asm
; Masked 8x8 sprite composition at free pixel coordinates.
;
; This replaces the Spectrum's XOR draw/erase with invalidation plus masked
; composition. Terrain is repainted from the visual layer by the caller;
; this routine then overlays the actor, writing only the pixels the glyph
; actually sets. Clear glyph bits leave the background untouched, so no
; erase pass is needed and no XOR residue can accumulate.
;
; Actors move in 2-pixel steps from cell-aligned starts, so X is always even.
; An even X is exactly byte-aligned in mode 3 (2 pixels per byte), which is
; why this routine needs no nibble shifting - a decisive simplification over
; the general case. The entry point rejects an odd X rather than drawing it
; incorrectly.
;
; The same two-phase split as tiles.asm applies: the glyph bank lives in low
; RAM, so it is staged into high memory first, and only the short transfer
; window runs with VRAM banked over $0000-$7FFF.

TILE_Y_OFFSET   equ 32

    SECTION bss_graphics

    PUBLIC  _spr_glyph
    PUBLIC  _spr_x
    PUBLIC  _spr_y
    PUBLIC  _spr_ink

_spr_rows:      defs 8          ; staged glyph bits (high memory)

_spr_glyph:     defs 2          ; pointer to 8 glyph bytes (low RAM)
_spr_x:         defs 1          ; 0..248, even
_spr_y:         defs 1          ; 0..175 (logical; offset applied here)
_spr_ink:       defs 1          ; palette index for set bits

_spr_dest:      defs 2          ; VRAM address of the sprite's top-left byte
_spr_pix:       defs 1          ; ink in both nibbles

    SECTION code_graphics

    PUBLIC  _sprite_draw_asm

    EXTERN  swapgfxbk
    EXTERN  swapgfxbk1

; ============================================================
; sprite_draw_asm - overlay one 8x8 glyph at (_spr_x, _spr_y).
; Set bits are painted in _spr_ink; clear bits are transparent.
; ============================================================
_sprite_draw_asm:
    ld      a, (_spr_x)
    and     a                   ; clear carry so RRA is a clean X>>1
    rra                         ; A = X>>1, carry = bit 0 of X
    jr      c, spr_done         ; odd X is unsupported, drop the draw

    push    af                  ; keep X>>1 across the glyph staging
    call    spr_stage
    pop     af
    ld      c, a                ; C = X>>1 for spr_calc_dest
    call    spr_calc_dest

    ld      a, (_spr_ink)
    and     $0F
    ld      b, a
    rlca
    rlca
    rlca
    rlca
    or      b
    ld      (_spr_pix), a       ; ink replicated into both nibbles

    call    swapgfxbk
    call    spr_transfer
    call    swapgfxbk1
spr_done:
    ret

; ------------------------------------------------------------
; spr_stage - copy the 8 glyph bytes out of low RAM.
; Must run with ordinary RAM mapped.
; ------------------------------------------------------------
spr_stage:
    ld      hl, (_spr_glyph)
    ld      de, _spr_rows
    ld      bc, 8
    ldir
    ret

; ------------------------------------------------------------
; spr_calc_dest - _spr_dest = (y + TILE_Y_OFFSET)*128 + (x >> 1)
; Entry: C = x >> 1 (read once by the caller; see note above).
; ------------------------------------------------------------
spr_calc_dest:
    ld      a, (_spr_y)
    add     a, TILE_Y_OFFSET    ; physical scanline (y <= 175, no overflow)
    ld      e, a

    srl     a
    ld      h, a                ; high byte = y >> 1
    ld      a, e
    rrca
    and     $80                 ; low byte = (y & 1) << 7
    ld      l, a                ; HL = y * 128

    ld      a, c                ; X >> 1 = byte offset within the scanline
    add     a, l
    ld      l, a
    jr      nc, scd_nocarry
    inc     h
scd_nocarry:
    ld      (_spr_dest), hl
    ret

; ------------------------------------------------------------
; spr_transfer - merge the staged glyph into VRAM.
;
; Runs with VRAM banked in. Reads only high memory (_spr_rows, _spr_pix)
; and read-modify-writes display RAM, so nothing in low RAM is touched
; while the bank is switched.
;
; Each VRAM byte holds two pixels: low nibble = left, high nibble = right.
; Per byte the two leading glyph bits are consumed, so one row is four
; bytes and the glyph byte is rotated twice per step.
; ------------------------------------------------------------
spr_transfer:
    ld      hl, _spr_rows
    ld      de, (_spr_dest)
    ld      b, 8
str_row:
    push    bc
    ld      c, (hl)             ; C = this row's 8 glyph bits
    push    hl
    ld      b, 4                ; 4 bytes = 8 pixels
str_byte:
    ld      a, (de)
    ld      l, a                ; L = current background byte

    ; --- left pixel: glyph bit 7 -> low nibble ---
    rlc     c
    jr      nc, str_noleft
    ld      a, l
    and     $F0                 ; keep the right pixel
    ld      h, a
    ld      a, (_spr_pix)
    and     $0F
    or      h
    ld      l, a
str_noleft:

    ; --- right pixel: glyph bit 6 (now bit 7) -> high nibble ---
    rlc     c
    jr      nc, str_noright
    ld      a, l
    and     $0F                 ; keep the left pixel
    ld      h, a
    ld      a, (_spr_pix)
    and     $F0
    or      h
    ld      l, a
str_noright:

    ld      a, l
    ld      (de), a
    inc     de
    djnz    str_byte

    pop     hl
    inc     hl                  ; next glyph row
    ; DE points one past the row; advance to the same column, next line.
    ld      a, e
    add     a, 124
    ld      e, a
    jr      nc, str_nocarry
    inc     d
str_nocarry:
    pop     bc
    djnz    str_row
    ret
