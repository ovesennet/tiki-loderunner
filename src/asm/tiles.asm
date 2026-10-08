; TIKI LODE RUNNER - tiles.asm
; 8x8 cell renderer: expand an original 1bpp Lode Runner glyph into a packed
; 4bpp Tiki tile, then transfer it to VRAM.
;
; This is the primitive the dirty-cell compositor is built on. The two phases
; are kept strictly separate because the glyph bank lives in ordinary low RAM:
;
;   1. Expand (VRAM NOT banked) - read _tile_glyph from low RAM into the
;      32-byte staging buffer _tile_buf, which lives in bss_graphics and so
;      is copied to high memory (CRT_ORG_GRAPHICS = $BB80) at startup.
;   2. Transfer (VRAM banked) - copy the staged 32 bytes into display RAM.
;      Only this short window runs with VRAM mapped over $0000-$7FFF, and it
;      touches nothing but high memory and VRAM.
;
; Mode 3 VRAM layout: 128 bytes per scanline, 2 pixels per byte,
; low nibble = left (even x), high nibble = right (odd x).
; A cell at (col,row) starts at VRAM byte (row*8 + TILE_Y_OFFSET)*128 + col*4.
;
; TILE_Y_OFFSET is the display-only offset that centres the Spectrum's
; 256x192 logical image on the Tiki's 256x256 raster. Gameplay coordinates
; never see it.

TILE_Y_OFFSET   equ 32

    SECTION bss_graphics

    PUBLIC  _tile_glyph
    PUBLIC  _tile_col
    PUBLIC  _tile_row
    PUBLIC  _tile_ink
    PUBLIC  _tile_paper

_tile_glyph:    defs 2          ; pointer to 8 glyph bytes (low RAM)
_tile_col:      defs 1          ; 0..31
_tile_row:      defs 1          ; 0..23 (logical rows, offset applied here)
_tile_ink:      defs 1          ; palette index for set bits
_tile_paper:    defs 1          ; palette index for clear bits

_tile_pairs:    defs 4          ; pixel-pair LUT for the current ink/paper
_tile_buf:      defs 32         ; staged 4bpp tile (8 rows x 4 bytes)
_tile_dest:     defs 2          ; VRAM destination of the staged tile

    SECTION code_graphics

    PUBLIC  _tile_draw_asm
    PUBLIC  _tile_fill_row_asm

    EXTERN  swapgfxbk
    EXTERN  swapgfxbk1

; ============================================================
; tile_draw_asm - draw one 8x8 cell.
; Params: _tile_glyph, _tile_col, _tile_row, _tile_ink, _tile_paper
; ============================================================
_tile_draw_asm:
    call    tile_build_pairs
    call    tile_calc_dest
    call    tile_expand
    call    swapgfxbk
    call    tile_transfer
    call    swapgfxbk1
    ret

; ------------------------------------------------------------
; tile_build_pairs - build the 4-entry pixel-pair LUT.
; Index = (left_bit << 1) | right_bit; left pixel goes in the low nibble.
; ------------------------------------------------------------
tile_build_pairs:
    ld      a, (_tile_paper)
    and     $0F
    ld      c, a                ; C = paper, low nibble
    ld      a, (_tile_ink)
    and     $0F
    ld      b, a                ; B = ink, low nibble

    ld      a, c
    rlca
    rlca
    rlca
    rlca
    ld      e, a                ; E = paper, high nibble
    ld      a, b
    rlca
    rlca
    rlca
    rlca
    ld      d, a                ; D = ink, high nibble

    ld      hl, _tile_pairs
    ld      a, c
    or      e
    ld      (hl), a             ; 00: paper paper
    inc     hl
    ld      a, c
    or      d
    ld      (hl), a             ; 01: paper ink
    inc     hl
    ld      a, b
    or      e
    ld      (hl), a             ; 10: ink paper
    inc     hl
    ld      a, b
    or      d
    ld      (hl), a             ; 11: ink ink
    ret

; ------------------------------------------------------------
; tile_calc_dest - _tile_dest = (row*8 + TILE_Y_OFFSET)*128 + col*4
; ------------------------------------------------------------
tile_calc_dest:
    ld      a, (_tile_row)
    add     a, a
    add     a, a
    add     a, a                ; row*8 (max 184, no overflow)
    add     a, TILE_Y_OFFSET    ; physical scanline of the cell's top row
    ld      e, a

    srl     a
    ld      h, a                ; high byte = y >> 1
    ld      a, e
    rrca
    and     $80                 ; low byte = (y & 1) << 7
    ld      l, a                ; HL = y * 128

    ld      a, (_tile_col)
    add     a, a
    add     a, a                ; col*4 bytes
    add     a, l
    ld      l, a
    jr      nc, tcd_nocarry
    inc     h
tcd_nocarry:
    ld      (_tile_dest), hl
    ret

; ------------------------------------------------------------
; tile_expand - 8 glyph bytes -> 32 staged bytes.
; Must run with ordinary RAM mapped: the glyph bank is in low memory.
; ------------------------------------------------------------
tile_expand:
    ld      hl, (_tile_glyph)
    ld      de, _tile_buf
    ld      b, 8
texp_row:
    push    bc
    ld      a, (hl)
    inc     hl
    ld      c, a                ; C = this row's 8 source bits
    push    hl
    ld      b, 4
texp_pair:
    ld      a, c
    rlca
    rlca                        ; bits 7,6 -> bits 1,0
    ld      c, a                ; keep rotating for the next pair
    and     $03
    ld      hl, _tile_pairs
    add     a, l
    ld      l, a
    jr      nc, texp_nocarry
    inc     h
texp_nocarry:
    ld      a, (hl)
    ld      (de), a
    inc     de
    djnz    texp_pair
    pop     hl
    pop     bc
    djnz    texp_row
    ret

; ------------------------------------------------------------
; tile_transfer - staged tile -> VRAM. Runs with VRAM banked in; reads only
; high memory (_tile_buf) and writes only display RAM.
; ------------------------------------------------------------
tile_transfer:
    ld      hl, _tile_buf
    ld      de, (_tile_dest)
    ld      b, 8
ttr_row:
    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    ; DE now points one past the row; advance to the same column, next line.
    ld      a, e
    add     a, 124
    ld      e, a
    jr      nc, ttr_nocarry
    inc     d
ttr_nocarry:
    djnz    ttr_row
    ret

; ============================================================
; tile_fill_row_asm - fill all 32 cells of logical row _tile_row with the
; flat colour _tile_ink. Used for the HUD separator and for clearing rows
; without paying 32 glyph expansions.
; ============================================================
_tile_fill_row_asm:
    ld      a, (_tile_ink)
    and     $0F
    ld      b, a
    rlca
    rlca
    rlca
    rlca
    or      b
    ld      c, a                ; C = both pixels of the byte

    ld      a, (_tile_row)
    add     a, a
    add     a, a
    add     a, a
    add     a, TILE_Y_OFFSET
    ld      e, a
    srl     a
    ld      h, a
    ld      a, e
    rrca
    and     $80
    ld      l, a                ; HL = y * 128, column 0

    call    swapgfxbk
    ld      b, 8                ; 8 scanlines
tfr_line:
    push    bc
    push    hl
    ld      b, 128              ; 128 bytes = 256 pixels
    ld      a, c
tfr_byte:
    ld      (hl), a
    inc     hl
    djnz    tfr_byte
    pop     hl
    ld      a, l
    add     a, 128
    ld      l, a
    jr      nc, tfr_nocarry
    inc     h
tfr_nocarry:
    pop     bc
    djnz    tfr_line
    call    swapgfxbk1
    ret
