#!/usr/bin/env python3
"""make-levelmap.py - Render every room into one large contact sheet.

Decodes the generated level tables on the host and draws each room exactly
as the game does: the same packed-room unpacking, the same glyph bank, the
same SPRITE_COLORS palette and the same RENDER_GAME rules (hidden bricks
look like bricks, hidden ladders look empty). Spawn positions are overlaid
with the man glyph, white for the player and bright magenta for guards.

No emulator needed, so this is also a quick way to see what the shipped
room set actually looks like after a change to the generator.

Usage:
  python tools/make-levelmap.py [-o out.png] [--cols N] [--scale N]
                                [--editor] [--no-spawns]
"""
import argparse
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("error: Pillow is required (pip install pillow)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM = os.path.join(ROOT, "src", "asm")

ROOM_COLS, ROOM_ROWS = 32, 22
ROOM_PACKED = 264               # 704 cells x 3 bits
MEN_RECORD = 12
MAX_ENEMIES = 5
CELL = 8                        # glyph is 8x8 pixels
GLYPH_TERRAIN = 0x20
GLYPH_MAN = 0x1E

OBJ_EMPTY, OBJ_BRICKS, OBJ_HIDBRICKS, OBJ_HIDLADDER = 0, 1, 5, 6

# video.c: the Spectrum's 16 attribute colours, RRRGGGBB.
PALETTE_BYTES = [0x00, 0x02, 0xC0, 0xC2, 0x18, 0x1A, 0xD8, 0xDA,
                 0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF]

# video.c sprite_colours[], indexed by object code.
COL_BRWHITE, COL_BRRED, COL_BRCYAN = 15, 10, 13
COL_BRGREEN, COL_BRYELLOW, COL_BRMAGENTA, COL_WHITE = 12, 14, 11, 7
SPRITE_COLOURS = [COL_BRWHITE, COL_BRRED, COL_BRRED, COL_BRWHITE,
                  COL_BRWHITE, COL_BRCYAN, COL_BRGREEN, COL_BRYELLOW]


def rgb(idx):
    """Expand one RRRGGGBB palette byte to 8-bit RGB."""
    b = PALETTE_BYTES[idx]
    r3, g3, b2 = (b >> 5) & 7, (b >> 2) & 7, b & 3
    return (r3 * 255 // 7, g3 * 255 // 7, b2 * 255 // 3)


def load_defb(path, symbol):
    """Pull one symbol's defb byte stream out of a generated .asm file."""
    buf = bytearray()
    grab = False
    with open(path, encoding="ascii") as fh:
        for line in fh:
            s = line.strip()
            if s.startswith("_") and s.endswith(":"):
                grab = (s == symbol + ":")
                continue
            if grab and s.startswith("defb"):
                buf.extend(int(v.strip()[1:], 16) for v in s[4:].split(","))
    if not buf:
        sys.exit("error: symbol %s not found in %s" % (symbol, path))
    return bytes(buf)


def unpack(packed):
    """704 cells of 3 bits, big-endian, exactly as room_unpack reads them."""
    bits = int.from_bytes(packed, "big")
    total = ROOM_COLS * ROOM_ROWS
    return [(bits >> (3 * (total - 1 - i))) & 7 for i in range(total)]


def draw_glyph(px, gfx, gx, gy, glyph, colour):
    """Paint one 8x8 glyph, set bits only -- a transparent overlay."""
    ink = rgb(colour)
    base = glyph * 8
    for row in range(8):
        bitsrow = gfx[base + row]
        if not bitsrow:
            continue
        y = gy + row
        for col in range(8):
            if bitsrow & (0x80 >> col):
                px[gx + col, y] = ink


def render_room(gfx, packed, men, editor, spawns):
    img = Image.new("RGB", (ROOM_COLS * CELL, ROOM_ROWS * CELL), (0, 0, 0))
    px = img.load()

    cells = unpack(packed)
    for i, obj in enumerate(cells):
        if not editor:
            # RENDER_GAME: hidden objects masquerade as their plain form.
            if obj == OBJ_HIDBRICKS:
                obj = OBJ_BRICKS
            elif obj == OBJ_HIDLADDER:
                obj = OBJ_EMPTY
        if obj == OBJ_EMPTY:
            continue
        row, col = divmod(i, ROOM_COLS)
        draw_glyph(px, gfx, col * CELL, row * CELL,
                   GLYPH_TERRAIN + obj, SPRITE_COLOURS[obj])

    if spawns and men:
        prow, pcol = men[0] & 0x1F, men[1] >> 3
        count = min(men[1] & 7, MAX_ENEMIES)
        if prow < ROOM_ROWS and pcol < ROOM_COLS:
            draw_glyph(px, gfx, pcol * CELL, prow * CELL, GLYPH_MAN, COL_WHITE)
        for e in range(count):
            erow, ecol = men[2 + e * 2] & 0x1F, men[3 + e * 2]
            if erow < ROOM_ROWS and ecol < ROOM_COLS:
                draw_glyph(px, gfx, ecol * CELL, erow * CELL,
                           GLYPH_MAN, COL_BRMAGENTA)
    return img


def render_label(gfx, text, width, height, colour):
    img = Image.new("RGB", (width, height), (0, 0, 0))
    px = img.load()
    # Glyph index is the ASCII code, exactly as hud_text passes it, so the
    # sheet is labelled in the game's own font with no font dependency.
    x = max(0, (width - len(text) * CELL) // 2)
    y = max(0, (height - CELL) // 2)
    for ch in text:
        if x + CELL > width:
            break
        draw_glyph(px, gfx, x, y, ord(ch) & 0xFF, colour)
        x += CELL
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "build", "levelmap.png"))
    ap.add_argument("--cols", type=int, default=6, help="rooms per row (default 6)")
    ap.add_argument("--scale", type=int, default=1, help="integer pixel zoom")
    ap.add_argument("--editor", action="store_true",
                    help="reveal hidden bricks and ladders")
    ap.add_argument("--no-spawns", action="store_true",
                    help="omit the player and guard markers")
    ap.add_argument("--gap", type=int, default=4, help="pixels between cells")
    args = ap.parse_args()

    gfx = load_defb(os.path.join(ASM, "gfxdata.asm"), "_gfx_bank")
    level = load_defb(os.path.join(ASM, "leveldata.asm"), "_level_data")
    men = load_defb(os.path.join(ASM, "leveldata.asm"), "_level_men")
    train = load_defb(os.path.join(ASM, "trainlevel.asm"), "_train_data")
    train_men = load_defb(os.path.join(ASM, "trainlevel.asm"), "_train_men")

    count = len(level) // ROOM_PACKED
    rooms = [("TRAINING", train[:ROOM_PACKED], train_men[:MEN_RECORD])]
    for n in range(1, count + 1):
        rooms.append(("ROOM %d" % n,
                      level[(n - 1) * ROOM_PACKED:n * ROOM_PACKED],
                      men[(n - 1) * MEN_RECORD:n * MEN_RECORD]))

    # A cell is the room at native size plus a label strip: 256x176 + 24.
    # The room is never resampled -- an 8x8 glyph does not survive a
    # fractional rescale, and the point of the sheet is to show the art.
    room_w, room_h = ROOM_COLS * CELL, ROOM_ROWS * CELL
    label_h = 24
    cell_w, cell_h = room_w, room_h + label_h

    cols = max(1, args.cols)
    rows = (len(rooms) + cols - 1) // cols
    gap = args.gap
    sheet_w = cols * cell_w + (cols + 1) * gap
    sheet_h = rows * cell_h + (rows + 1) * gap
    sheet = Image.new("RGB", (sheet_w, sheet_h), (16, 16, 16))

    for i, (name, packed, rec) in enumerate(rooms):
        r, c = divmod(i, cols)
        x = gap + c * (cell_w + gap)
        y = gap + r * (cell_h + gap)
        colour = COL_BRYELLOW if i == 0 else COL_BRWHITE
        sheet.paste(render_label(gfx, name, cell_w, label_h, colour), (x, y))
        sheet.paste(render_room(gfx, packed, rec, args.editor,
                                not args.no_spawns), (x, y + label_h))

    if args.scale > 1:
        sheet = sheet.resize((sheet_w * args.scale, sheet_h * args.scale),
                             Image.NEAREST)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    sheet.save(args.out)
    print("%d rooms, %d x %d grid -> %s (%d x %d)"
          % (len(rooms), cols, rows, args.out, sheet.width, sheet.height))


if __name__ == "__main__":
    main()
