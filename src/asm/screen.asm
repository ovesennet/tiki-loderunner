; TIKI LODE RUNNER - screen.asm
; Low-level VRAM drawing routines for Tiki-100 mode 3 (256x256x16).
; Platform layer adapted from the Tiki-Galaga/Arkanoid port.
;
; All routines in SECTION code_graphics (high memory, above 0x8000)
; so they remain accessible when VRAM is banked into 0x0000-0x7FFF.
;
; Mode 3 VRAM layout:
;   128 bytes per scanline, 256 scanlines = 32768 bytes
;   2 pixels per byte: bits 3-0 = left (even x), bits 7-4 = right (odd x)
;   Byte address = y * 128 + x / 2

    SECTION code_graphics

    PUBLIC  _vid_clear_asm
    PUBLIC  _vid_plot_gfx
    PUBLIC  _vid_hline_gfx
    PUBLIC  _vid_vline_gfx
    PUBLIC  _vid_fill_rect_gfx
    PUBLIC  _vid_draw_text_gfx
    PUBLIC  _vid_begin_vram_asm
    PUBLIC  _vid_end_vram_asm
    PUBLIC  _vid_fill_rect_nr_asm

    EXTERN  swapgfxbk
    EXTERN  swapgfxbk1

; ============================================================
; Global parameter block for C -> ASM communication
; ============================================================
    SECTION bss_graphics

    PUBLIC  _gfx_x1
    PUBLIC  _gfx_y1
    PUBLIC  _gfx_x2
    PUBLIC  _gfx_y2
    PUBLIC  _gfx_colour
    PUBLIC  _gfx_width
    PUBLIC  _gfx_height

_gfx_x1:       defs 2
_gfx_y1:       defs 2
_gfx_x2:       defs 2
_gfx_y2:       defs 2
_gfx_colour:   defs 1
_gfx_width:    defs 1
_gfx_height:   defs 1

; Scratch for hline
_hl_base:      defs 2
_hl_x1:        defs 1
_hl_x2:        defs 1
_hl_colbyte:   defs 1
_hl_colnib:    defs 1

; Text draw parameters
    PUBLIC  _txt_x
    PUBLIC  _txt_y
    PUBLIC  _txt_str
    PUBLIC  _txt_colour

_txt_x:        defs 2
_txt_y:        defs 2
_txt_str:      defs 2
_txt_colour:   defs 1

; Text scratch
_txt_colbyte:  defs 1
_txt_vram:     defs 2
_txt_glyph:    defs 2
_txt_rot:      defs 5

    SECTION code_graphics

; ============================================================
; vid_clear_asm ? zero all 32K VRAM
; ============================================================
_vid_clear_asm:
    call    swapgfxbk
    ld      hl, 0
    ld      de, 1
    ld      bc, 32767
    ld      (hl), 0
    ldir
    call    swapgfxbk1
    ret

; ============================================================
; vid_plot_gfx ? plot single pixel
; ============================================================
_vid_plot_gfx:
    ld      hl, (_gfx_y1)
    ld      a, h
    or      a
    ret     nz
    ld      d, l

    ld      hl, (_gfx_x1)
    ld      a, h
    or      a
    ret     nz
    ld      e, l

    ld      a, (_gfx_colour)
    ld      c, a

    ; VRAM address = y * 128 + x / 2
    ld      a, d
    srl     a
    ld      h, a
    ld      a, d
    rrca
    and     $80
    ld      l, a
    ld      a, e
    srl     a
    add     a, l
    ld      l, a
    jr      nc, plot_noc
    inc     h
plot_noc:
    call    swapgfxbk
    bit     0, e
    jr      nz, plot_odd
    ; Even pixel: low nibble
    ld      a, c
    and     $0F
    ld      b, a
    ld      a, (hl)
    and     $F0
    or      b
    ld      (hl), a
    call    swapgfxbk1
    ret
plot_odd:
    ; Odd pixel: high nibble
    ld      a, c
    rlca
    rlca
    rlca
    rlca
    and     $F0
    ld      b, a
    ld      a, (hl)
    and     $0F
    or      b
    ld      (hl), a
    call    swapgfxbk1
    ret

; ============================================================
; vid_hline_gfx ? fast horizontal line
; ============================================================
_vid_hline_gfx:
    ld      hl, (_gfx_y1)
    ld      a, h
    or      a
    ret     nz

    ; Compute scanline base: y * 128
    ld      a, l
    srl     a
    ld      h, a
    ld      a, (_gfx_y1)
    rrca
    and     $80
    ld      l, a
    ld      (_hl_base), hl

    ; Sort x1, x2
    ld      a, (_gfx_x1)
    ld      b, a
    ld      a, (_gfx_x2)
    ld      c, a
    ld      a, b
    cp      c
    jr      c, hl_sorted
    jr      z, hl_sorted
    ld      b, c
    ld      c, a
hl_sorted:
    ld      a, b
    ld      (_hl_x1), a
    ld      a, c
    ld      (_hl_x2), a

    ; Build colour bytes
    ld      a, (_gfx_colour)
    and     $0F
    ld      (_hl_colnib), a
    ld      b, a
    rlca
    rlca
    rlca
    rlca
    or      b
    ld      (_hl_colbyte), a

    call    swapgfxbk
    call    hline_raw_body
    call    swapgfxbk1
    ret

    ; Phase 1: Left partial byte (x1 odd)
hline_raw_body:
    ld      a, (_hl_x1)
    bit     0, a
    jr      z, hl_no_left

    srl     a
    ld      hl, (_hl_base)
    add     a, l
    ld      l, a
    jr      nc, hl_lp_noc
    inc     h
hl_lp_noc:
    ld      a, (_hl_colbyte)
    and     $F0
    ld      b, a
    ld      a, (hl)
    and     $0F
    or      b
    ld      (hl), a

    ld      a, (_hl_x1)
    inc     a
    ld      (_hl_x1), a
    ld      b, a
    ld      a, (_hl_x2)
    cp      b
    jp      c, hl_done_raw

hl_no_left:
    ; Phase 3: Right partial byte (x2 even)
    ld      a, (_hl_x2)
    bit     0, a
    jr      nz, hl_no_right

    srl     a
    ld      hl, (_hl_base)
    add     a, l
    ld      l, a
    jr      nc, hl_rp_noc
    inc     h
hl_rp_noc:
    ld      a, (_hl_colnib)
    ld      b, a
    ld      a, (hl)
    and     $F0
    or      b
    ld      (hl), a

    ld      a, (_hl_x2)
    dec     a
    ld      (_hl_x2), a
    ld      b, a
    ld      a, (_hl_x1)
    ld      c, a
    ld      a, b
    cp      c
    jp      c, hl_done_raw

hl_no_right:
    ; Phase 2: Middle full bytes
    ld      a, (_hl_x1)
    srl     a
    ld      c, a

    ld      a, (_hl_x2)
    srl     a
    sub     c
    inc     a
    ld      b, a

    ld      hl, (_hl_base)
    ld      a, c
    add     a, l
    ld      l, a
    jr      nc, hl_mid_noc
    inc     h
hl_mid_noc:
    ld      a, (_hl_colbyte)

hl_fill:
    ld      (hl), a
    inc     hl
    djnz    hl_fill

hl_done_raw:
    ret

; hline_raw ? same as hline_gfx but assumes VRAM already banked in.
; Params already set in _gfx_x1, _gfx_y1, _gfx_x2, _gfx_colour.
hline_raw:
    ld      hl, (_gfx_y1)
    ld      a, h
    or      a
    ret     nz
    ld      a, l
    srl     a
    ld      h, a
    ld      a, (_gfx_y1)
    rrca
    and     $80
    ld      l, a
    ld      (_hl_base), hl
    ld      a, (_gfx_x1)
    ld      b, a
    ld      a, (_gfx_x2)
    ld      c, a
    ld      a, b
    cp      c
    jr      c, hlr_sorted
    jr      z, hlr_sorted
    ld      b, c
    ld      c, a
hlr_sorted:
    ld      a, b
    ld      (_hl_x1), a
    ld      a, c
    ld      (_hl_x2), a
    ld      a, (_gfx_colour)
    and     $0F
    ld      (_hl_colnib), a
    ld      b, a
    rlca
    rlca
    rlca
    rlca
    or      b
    ld      (_hl_colbyte), a
    jp      hline_raw_body

; fill_rect_raw ? filled rectangle, assumes VRAM already banked in.
; Params: _gfx_x1, _gfx_y1, _gfx_width, _gfx_height, _gfx_colour
fill_rect_raw:
    ld      hl, (_gfx_x1)
    ld      a, (_gfx_width)
    dec     a
    add     a, l
    ld      l, a
    ld      a, 0
    adc     a, h
    ld      h, a
    ld      (_gfx_x2), hl
    ld      a, (_gfx_height)
    ld      b, a
    or      a
    ret     z
frr_loop:
    push    bc
    call    hline_raw
    pop     bc
    ld      hl, (_gfx_y1)
    inc     hl
    ld      (_gfx_y1), hl
    ld      a, h
    or      a
    ret     nz
    djnz    frr_loop
    ret

; ============================================================
; vid_vline_gfx ? fast vertical line
; ============================================================
_vid_vline_gfx:
    ld      hl, (_gfx_x1)
    ld      a, h
    or      a
    ret     nz

    ld      a, l
    ld      e, a
    srl     a

    ld      d, a
    ld      hl, (_gfx_y1)
    ld      a, l
    srl     a
    ld      h, a
    ld      a, (_gfx_y1)
    rrca
    and     $80
    add     a, d
    ld      l, a
    jr      nc, vl_noc
    inc     h
vl_noc:
    ld      a, (_gfx_colour)
    and     $0F
    bit     0, e
    jr      nz, vl_odd_prep

    ld      c, a
    ld      b, $F0
    jr      vl_prep_done

vl_odd_prep:
    rlca
    rlca
    rlca
    rlca
    ld      c, a
    ld      b, $0F

vl_prep_done:
    ld      a, (_gfx_y2)
    ld      d, a
    ld      a, (_gfx_y1)
    ld      e, a
    ld      a, d
    sub     e
    inc     a
    ld      d, a

    call    swapgfxbk

vl_loop:
    ld      a, (hl)
    and     b
    or      c
    ld      (hl), a

    ld      a, l
    add     a, 128
    ld      l, a
    jr      nc, vl_no_carry
    inc     h
vl_no_carry:
    dec     d
    jr      nz, vl_loop

    call    swapgfxbk1
    ret

; ============================================================
; vid_fill_rect_gfx ? fast filled rectangle
; ============================================================
_vid_fill_rect_gfx:
    ld      hl, (_gfx_x1)
    ld      a, (_gfx_width)
    dec     a
    add     a, l
    ld      l, a
    ld      a, 0
    adc     a, h
    ld      h, a
    ld      (_gfx_x2), hl

    ld      a, (_gfx_height)
    ld      b, a
    or      a
    ret     z

    call    swapgfxbk

fr_loop:
    push    bc
    call    hline_raw
    pop     bc

    ld      hl, (_gfx_y1)
    inc     hl
    ld      (_gfx_y1), hl
    ld      a, h
    or      a
    jr      nz, fr_done

    djnz    fr_loop
fr_done:
    call    swapgfxbk1
    ret

; ============================================================
; vid_begin_vram / vid_end_vram ? bracket for batched VRAM ops
; ============================================================
_vid_begin_vram_asm:
    call    swapgfxbk
    ret

_vid_end_vram_asm:
    call    swapgfxbk1
    ret

; ============================================================
; vid_fill_rect_nr ? fill rect, NO bank switch (caller must bracket)
; Uses same globals: _gfx_x1, _gfx_y1, _gfx_width, _gfx_height, _gfx_colour
; ============================================================
_vid_fill_rect_nr_asm:
    call    fill_rect_raw
    ret

; ============================================================
; Frame counter & vsync
; The CP/M interrupt handler at $F057 is patched to jump to
; our tiny ISR that increments a frame counter, then RETs.
; wait_vsync spins until the counter changes ? never misses
; a frame even if game logic takes > 20ms.
; ============================================================

    SECTION bss_graphics

    PUBLIC  _frame_counter
_frame_counter:  defs 1
_saved_f057:     defs 3          ; save original 3 bytes at $F057

    SECTION code_graphics

    PUBLIC  _wait_vsync_asm
    PUBLIC  _vsync_init_asm
    PUBLIC  _vsync_shutdown_asm

; Tiny ISR ? just increment the frame counter and return.
_frame_isr:
    push    af
    ld      a, (_frame_counter)
    inc     a
    ld      (_frame_counter), a
    pop     af
    ret

; Install our ISR: save original $F057-$F059, patch with JP _frame_isr
_vsync_init_asm:
    ; Save original 3 bytes
    ld      a, ($F057)
    ld      (_saved_f057), a
    ld      a, ($F058)
    ld      (_saved_f057+1), a
    ld      a, ($F059)
    ld      (_saved_f057+2), a
    ; Patch: JP _frame_isr
    ld      a, $C3
    ld      ($F057), a
    ld      hl, _frame_isr
    ld      ($F058), hl
    xor     a
    ld      (_frame_counter), a
    ret

; Restore original 3 bytes at $F057
_vsync_shutdown_asm:
    ld      a, (_saved_f057)
    ld      ($F057), a
    ld      a, (_saved_f057+1)
    ld      ($F058), a
    ld      a, (_saved_f057+2)
    ld      ($F059), a
    ret

; Wait for the next frame tick, with a bounded fallback.
;
; The $F057 hook is a firmware callback, not a true frame interrupt: on this
; ROM it fires at roughly the cursor blink rate, and on some paths not at all.
; Blocking on it indefinitely would stall the poll loop and drop keypresses,
; so the wait gives up after about 20 ms of spinning (~4 MHz, ~40 T per
; iteration) and returns regardless. A CTC timer would give real cadence
; control.
_wait_vsync_asm:
    ld      de, 2000            ; timeout iterations ~= 20 ms
    ld      a, (_frame_counter)
    ld      b, a
_wv_loop:
    ld      a, (_frame_counter)
    cp      b
    ret     nz
    dec     de
    ld      a, d
    or      e
    jr      nz, _wv_loop
    ret


; ============================================================
; Font data ? 5x7 pixel glyphs (thick arcade style, 2px strokes)
; ============================================================
_font_gfx:
    ; A ? flat top, thick bars
    defb $0E, $1B, $1B, $1F, $1B, $1B, $1B
    ; B
    defb $1E, $1B, $1B, $1E, $1B, $1B, $1E
    ; C
    defb $0E, $1B, $18, $18, $18, $1B, $0E
    ; D
    defb $1E, $1B, $1B, $1B, $1B, $1B, $1E
    ; E
    defb $1F, $18, $18, $1E, $18, $18, $1F
    ; F
    defb $1F, $18, $18, $1E, $18, $18, $18
    ; G
    defb $0E, $1B, $18, $1B, $1B, $1B, $0E
    ; H
    defb $1B, $1B, $1B, $1F, $1B, $1B, $1B
    ; I ? full-width serifs
    defb $1F, $04, $04, $04, $04, $04, $1F
    ; J
    defb $0F, $03, $03, $03, $03, $1B, $0E
    ; K
    defb $1B, $1A, $1C, $18, $1C, $1A, $1B
    ; L
    defb $18, $18, $18, $18, $18, $18, $1F
    ; M
    defb $1B, $1F, $15, $1B, $1B, $1B, $1B
    ; N
    defb $1B, $1B, $1F, $1F, $1B, $1B, $1B
    ; O
    defb $0E, $1B, $1B, $1B, $1B, $1B, $0E
    ; P
    defb $1E, $1B, $1B, $1E, $18, $18, $18
    ; Q
    defb $0E, $1B, $1B, $1B, $1B, $0E, $03
    ; R
    defb $1E, $1B, $1B, $1E, $1A, $1B, $1B
    ; S
    defb $0E, $1B, $18, $0E, $03, $1B, $0E
    ; T
    defb $1F, $04, $04, $04, $04, $04, $04
    ; U
    defb $1B, $1B, $1B, $1B, $1B, $1B, $0E
    ; V
    defb $1B, $1B, $1B, $1B, $1B, $0A, $04
    ; W
    defb $1B, $1B, $1B, $1B, $15, $1F, $0A
    ; X
    defb $1B, $1B, $0E, $04, $0E, $1B, $1B
    ; Y
    defb $1B, $1B, $0E, $04, $04, $04, $04
    ; Z
    defb $1F, $03, $06, $04, $0C, $18, $1F
    ; 0
    defb $0E, $1B, $1B, $1B, $1B, $1B, $0E
    ; 1
    defb $0C, $1C, $0C, $0C, $0C, $0C, $1F
    ; 2
    defb $0E, $1B, $03, $06, $0C, $18, $1F
    ; 3
    defb $0E, $1B, $03, $0E, $03, $1B, $0E
    ; 4
    defb $1B, $1B, $1B, $1F, $03, $03, $03
    ; 5
    defb $1F, $18, $1E, $03, $03, $1B, $0E
    ; 6
    defb $0E, $18, $18, $1E, $1B, $1B, $0E
    ; 7
    defb $1F, $03, $03, $06, $0C, $0C, $0C
    ; 8
    defb $0E, $1B, $1B, $0E, $1B, $1B, $0E
    ; 9
    defb $0E, $1B, $1B, $0F, $03, $03, $0E
    ; space
    defb $00, $00, $00, $00, $00, $00, $00
    ; !
    defb $04, $04, $04, $04, $04, $00, $04
    ; .
    defb $00, $00, $00, $00, $00, $00, $04
    ; :
    defb $00, $04, $00, $00, $00, $04, $00
    ; -
    defb $00, $00, $00, $0E, $00, $00, $00

; ============================================================
; ASCII-to-glyph lookup table (128 entries)
; ============================================================
_char_map:
    ; 0x00-0x0F
    defb 36,36,36,36,36,36,36,36, 36,36,36,36,36,36,36,36
    ; 0x10-0x1F
    defb 36,36,36,36,36,36,36,36, 36,36,36,36,36,36,36,36
    ; 0x20-0x2F: ' ' ! " # $ % & ' ( ) * + , - . /
    defb 36,37,36,36,36,36,36,36, 36,36,36,36,36,40,38,36
    ; 0x30-0x3F: 0-9 : ; < = > ?
    defb 26,27,28,29,30,31,32,33, 34,35,39,36,36,36,36,36
    ; 0x40-0x4F: @ A-O
    defb 36, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9,10,11,12,13,14
    ; 0x50-0x5F: P-Z
    defb 15,16,17,18,19,20,21,22, 23,24,25,36,36,36,36,36
    ; 0x60-0x6F: ` a-o (lowercase = uppercase)
    defb 36, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9,10,11,12,13,14
    ; 0x70-0x7F: p-z
    defb 15,16,17,18,19,20,21,22, 23,24,25,36,36,36,36,36

; ============================================================
; vid_draw_text_gfx ? fast text rendering
; ============================================================

    SECTION bss_graphics
_txt_strbuf:   defs 32
    SECTION code_graphics

_vid_draw_text_gfx:
    push    ix

    ; Copy string to high-memory buffer (max 31 chars + null)
    ld      hl, (_txt_str)
    ld      de, _txt_strbuf
    ld      b, 31
txt_strcpy:
    ld      a, (hl)
    ld      (de), a
    or      a
    jr      z, txt_strcpy_done
    inc     hl
    inc     de
    djnz    txt_strcpy
    xor     a
    ld      (de), a
txt_strcpy_done:

    ; Prepare colour byte
    ld      a, (_txt_colour)
    and     $0F
    ld      b, a
    rlca
    rlca
    rlca
    rlca
    or      b
    ld      (_txt_colbyte), a

    call    swapgfxbk

    ld      ix, _txt_strbuf

; Character loop
txt_char_loop:
    ld      a, (ix+0)
    or      a
    jp      z, txt_done
    inc     ix

    ; Map ASCII to glyph index
    and     $7F
    ld      e, a
    ld      d, 0
    ld      hl, _char_map
    add     hl, de
    ld      a, (hl)

    ; Compute glyph pointer: _font_gfx + index * 7
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    add     hl, hl
    ld      e, a
    ld      d, 0
    or      a
    sbc     hl, de
    ld      de, _font_gfx
    add     hl, de
    ld      (_txt_glyph), hl

    ; Compute VRAM address: y * 128 + x / 2
    ld      hl, (_txt_y)
    ld      a, l
    srl     a
    ld      h, a
    ld      a, (_txt_y)
    rrca
    and     $80
    ld      l, a

    ld      a, (_txt_x)
    srl     a
    add     a, l
    ld      l, a
    jr      nc, txt_noc1
    inc     h
txt_noc1:
    ; HL = VRAM address

    ld      b, 7            ; 7 rows

    ld      a, (_txt_x)
    and     $01
    ld      c, a            ; C = alignment

    ld      de, (_txt_glyph)

txt_row_loop:
    push    bc
    push    hl

    ld      a, (de)
    inc     de
    push    de

    ld      b, a
    ld      a, c
    or      a
    jr      nz, txt_row_odd

    ; EVEN alignment
    ; Byte 0: pixel 0 (bit4) -> low nibble, pixel 1 (bit3) -> high nibble
    ld      a, (hl)
    and     $F0

    bit     4, b
    jr      z, txt_e0_bg
    ld      c, a
    ld      a, (_txt_colbyte)
    and     $0F
    or      c
    jr      txt_e0_done
txt_e0_bg:
txt_e0_done:
    ld      c, a

    bit     3, b
    jr      z, txt_e1_bg
    ld      a, (_txt_colbyte)
    and     $F0
    or      c
    jr      txt_e1_done
txt_e1_bg:
    ld      a, c
txt_e1_done:
    ld      (hl), a
    inc     hl

    ; Byte 1: pixel 2 (bit2) -> low, pixel 3 (bit1) -> high
    ld      a, 0

    bit     2, b
    jr      z, txt_e2_bg
    ld      a, (_txt_colbyte)
    and     $0F
txt_e2_bg:
    ld      c, a

    bit     1, b
    jr      z, txt_e3_bg
    ld      a, (_txt_colbyte)
    and     $F0
    or      c
    jr      txt_e3_done
txt_e3_bg:
    ld      a, c
txt_e3_done:
    ld      (hl), a
    inc     hl

    ; Byte 2: pixel 4 (bit0) -> low nibble, preserve high nibble
    ld      a, (hl)
    and     $F0

    bit     0, b
    jr      z, txt_e4_bg
    ld      c, a
    ld      a, (_txt_colbyte)
    and     $0F
    or      c
    jr      txt_e4_done
txt_e4_bg:
txt_e4_done:
    ld      (hl), a

    jr      txt_row_end

    ; ODD alignment
txt_row_odd:
    ; Byte 0: preserve low nibble, pixel 0 -> high nibble
    ld      a, (hl)
    and     $0F

    bit     4, b
    jr      z, txt_o0_bg
    ld      c, a
    ld      a, (_txt_colbyte)
    and     $F0
    or      c
    jr      txt_o0_done
txt_o0_bg:
txt_o0_done:
    ld      (hl), a
    inc     hl

    ; Byte 1: pixel 1 (bit3) -> low, pixel 2 (bit2) -> high
    ld      a, 0

    bit     3, b
    jr      z, txt_o1_bg
    ld      a, (_txt_colbyte)
    and     $0F
txt_o1_bg:
    ld      c, a

    bit     2, b
    jr      z, txt_o2_bg
    ld      a, (_txt_colbyte)
    and     $F0
    or      c
    jr      txt_o2_done
txt_o2_bg:
    ld      a, c
txt_o2_done:
    ld      (hl), a
    inc     hl

    ; Byte 2: pixel 3 (bit1) -> low, pixel 4 (bit0) -> high
    ld      a, 0

    bit     1, b
    jr      z, txt_o3_bg
    ld      a, (_txt_colbyte)
    and     $0F
txt_o3_bg:
    ld      c, a

    bit     0, b
    jr      z, txt_o4_bg
    ld      a, (_txt_colbyte)
    and     $F0
    or      c
    jr      txt_o4_done
txt_o4_bg:
    ld      a, c
txt_o4_done:
    ld      (hl), a

txt_row_end:
    pop     de
    pop     hl
    pop     bc

    ; Next scanline (+128)
    ld      a, l
    add     a, 128
    ld      l, a
    jr      nc, txt_row_nc
    inc     h
txt_row_nc:
    dec     b
    jp      nz, txt_row_loop

    ; Advance x by 6 (FONT_W + FONT_SPACING)
    ld      hl, (_txt_x)
    ld      de, 6
    add     hl, de
    ld      (_txt_x), hl

    jp      txt_char_loop

txt_done:
    call    swapgfxbk1
    pop     ix
    ret
