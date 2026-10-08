#!/usr/bin/env python3
"""Generate src/asm/trainlevel.asm from the ASCII map below.

Room 0 is an original training level written for this port -- it is not one
of the 75 rooms extracted from the Spectrum original, which is why it lives
in its own file rather than in the generated leveldata.asm.

The map is the source of truth. Edit MAP, re-run this script, rebuild.

Legend (OBJECT.* codes from Lode_constants.asm):
    .  empty        #  bricks (diggable)   =  solid (not diggable)
    H  ladder       -  rope                h  hidden ladder (the exit)
    $  gold

Layout, bottom to top -- each lesson is introduced on its own tier and the
tier below is always a safe place to land:

    rows 16-19  basement    walk right, collect gold, find a ladder
    rows 11-14  tier B      ladders, and a brick strip with gold visible
                            straight underneath it to invite a dig
    rows  6- 9  tier C      a rope over a gap, and a diggable patch
    rows  0- 4  tier D      diggable floor to trap a pursuer, then the
                            hidden exit ladder at the centre

The single guard patrols tier B, one tier above the player's start. The
guard AI seeks the player's column and only descends when it is standing on
a ladder, so a guard parked on the top tier would never arrive at all and a
guard in the basement would be on top of the player immediately. Tier B
gives the basement lessons room to breathe, puts the meeting on the tier
whose floor has the diggable strip, and leaves the rope and the floor gap
as escape routes.

Falling is never fatal in Lode Runner, every tier below is open, and no
chamber is sealed, so the room cannot be soft-locked.
"""

import os

MAP = [
    "=...............h..............=",   # 0
    "=...............h..............=",   # 1
    "=...............h..............=",   # 2
    "=...............h..............=",   # 3
    "=........$......h.......H......=",   # 4   tier D walkway
    "====#####=========#####=H=======",   # 5   tier D floor, diggable patches
    "=.......................H......=",   # 6
    "=.......................H......=",   # 7
    "=.......................H......=",   # 8
    "=.H.........-------.$...H......=",   # 9   tier C walkway, rope over gap
    "==H==###====.......=============",   # 10  tier C floor
    "=.H..........................H.=",   # 11
    "=.H..........................H.=",   # 12
    "=.H..........................H.=",   # 13
    "=.H....$............$........H.=",   # 14  tier B walkway
    "============####=============H==",   # 15  tier B floor, dig strip
    "=............................H.=",   # 16
    "=............................H.=",   # 17
    "=............................H.=",   # 18
    "=.....$......$...............H.=",   # 19  basement walkway
    "================================",   # 20  bedrock
    "================================",   # 21
]

CODES = {
    ".": 0,   # OBJ_EMPTY
    "#": 1,   # OBJ_BRICKS
    "=": 2,   # OBJ_SOLID
    "H": 3,   # OBJ_LADDER
    "-": 4,   # OBJ_ROPE
    "B": 5,   # OBJ_HIDBRICKS
    "h": 6,   # OBJ_HIDLADDER
    "$": 7,   # OBJ_BOX
}

COLS, ROWS = 32, 22
PACKED = 264
MEN_RECORD = 12
MAX_ENEMIES = 5

PLAYER = (19, 3)            # (row, col)
ENEMIES = [(14, 16)]        # (row, col), at most MAX_ENEMIES


def cells():
    if len(MAP) != ROWS:
        raise SystemExit("MAP must have %d rows, has %d" % (ROWS, len(MAP)))
    out = []
    for r, line in enumerate(MAP):
        if len(line) != COLS:
            raise SystemExit("row %d is %d wide, must be %d" % (r, len(line), COLS))
        for c, ch in enumerate(line):
            if ch not in CODES:
                raise SystemExit("row %d col %d: unknown glyph %r" % (r, c, ch))
            out.append(CODES[ch])
    return out


def pack(values):
    """704 cells as a big-endian 3-bit stream, the layout room_unpack reads."""
    bits = "".join(format(v, "03b") for v in values)
    bits += "0" * (PACKED * 8 - len(bits))
    return bytes(int(bits[i:i + 8], 2) for i in range(0, PACKED * 8, 8))


def spawn_record():
    prow, pcol = PLAYER
    if len(ENEMIES) > MAX_ENEMIES:
        raise SystemExit("at most %d guards" % MAX_ENEMIES)
    rec = [prow & 0x1F, ((pcol & 0x1F) << 3) | len(ENEMIES)]
    for i in range(MAX_ENEMIES):
        if i < len(ENEMIES):
            rec += [ENEMIES[i][0], ENEMIES[i][1]]
        else:
            rec += [0x01, 0x01]     # the original's unused-slot marker
    return bytes(rec)


def reachable(values, hidden_as_ladder):
    """Flood the room under the player's movement rules.

    Conservative: digging is ignored, so anything this finds is reachable
    without it. Used to prove every piece of gold and the exit can be got at.
    """
    EMPTY, BRICKS, SOLID, LADDER, ROPE, HIDBRICKS, HIDLADDER, BOX = range(8)

    def obj(row, col):
        o = values[row * COLS + col]
        if o == HIDBRICKS:
            return BRICKS
        if o == HIDLADDER:
            return LADDER if hidden_as_ladder else EMPTY
        return o

    def open_cell(row, col):
        if row < 0 or row >= ROWS or col < 0 or col >= COLS:
            return False
        return obj(row, col) in (EMPTY, LADDER, ROPE, BOX)

    def supported(row, col):
        if obj(row, col) in (LADDER, ROPE):
            return True
        if row + 1 >= ROWS:
            return True
        return obj(row + 1, col) in (BRICKS, SOLID, LADDER)

    def settle(row, col):
        """Fall until something holds, the way an unsupported actor does."""
        while not supported(row, col) and open_cell(row + 1, col):
            row += 1
        return (row, col)

    start = settle(*PLAYER)
    seen = {start}
    queue = [start]
    while queue:
        row, col = queue.pop()
        here = obj(row, col)
        moves = []
        for dc in (-1, 1):
            if open_cell(row, col + dc):
                moves.append(settle(row, col + dc))
        if here == LADDER and open_cell(row - 1, col):
            moves.append((row - 1, col))
        if open_cell(row + 1, col):
            moves.append(settle(row + 1, col))
        for m in moves:
            if m not in seen:
                seen.add(m)
                queue.append(m)
    return seen


def check(values):
    """Guard the invariants the room has to satisfy to be completable."""
    def at(row, col):
        return values[row * COLS + col]

    gold = [(r, c) for r in range(ROWS) for c in range(COLS) if at(r, c) == 7]
    if not gold:
        raise SystemExit("no gold: the exit ladder would never appear")

    exits = [(r, c) for r in range(ROWS) for c in range(COLS) if at(r, c) == 6]
    if not exits:
        raise SystemExit("no hidden ladder: the room cannot be completed")
    if not any(r == 0 for r, _ in exits):
        raise SystemExit("the hidden ladder does not reach row 0")

    prow, pcol = PLAYER
    if at(prow, pcol) not in (0, 7):
        raise SystemExit("the player spawns inside terrain")
    for erow, ecol in ENEMIES:
        if at(erow, ecol) != 0:
            raise SystemExit("a guard spawns inside terrain")

    # Every piece of gold has to be collectable before the exit appears...
    before = reachable(values, hidden_as_ladder=False)
    missed = [g for g in gold if g not in before]
    if missed:
        raise SystemExit("gold unreachable without digging: %s" % missed)

    # ...and the top of the exit ladder reachable once it does.
    after = reachable(values, hidden_as_ladder=True)
    if not any((0, c) in after for c in range(COLS)):
        raise SystemExit("the top row is unreachable even with the exit ladder")

    return len(gold)


def emit(path, packed, men, gold):
    lines = [
        "; trainlevel.asm - GENERATED by tools/make-trainlevel.py. Do not edit.",
        ";",
        "; Room 0: an original training level for this port, in the same packed",
        "; form as leveldata.asm (704 cells x 3 bits, then a 12-byte spawn",
        "; record). It is not one of the 75 Spectrum rooms, so it is kept out of",
        "; the generated level data and selected separately by room.c.",
        ";",
        "; %d gold, %d guard(s), player at row %d col %d."
        % (gold, len(ENEMIES), PLAYER[0], PLAYER[1]),
        "",
        "    SECTION rodata_user",
        "",
        "    PUBLIC  _train_data",
        "    PUBLIC  _train_men",
        "",
        "_train_data:",
    ]
    for i in range(0, len(packed), 16):
        row = ",".join("$%02X" % b for b in packed[i:i + 16])
        lines.append("    defb " + row)
    lines += ["", "_train_men:",
              "    defb " + ",".join("$%02X" % b for b in men), ""]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "src", "asm", "trainlevel.asm")

    values = cells()
    gold = check(values)
    packed = pack(values)
    men = spawn_record()

    if len(packed) != PACKED or len(men) != MEN_RECORD:
        raise SystemExit("bad record size")

    emit(out, packed, men, gold)
    print("Wrote %s (%d + %d bytes, %d gold, %d guard(s))"
          % (out, len(packed), len(men), gold, len(ENEMIES)))


if __name__ == "__main__":
    main()
