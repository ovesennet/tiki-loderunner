"""Assert a gameplay snapshot actually rendered, not just that it changed.

A black screen with two sprites on it satisfies most "did anything move?"
metrics, which is exactly how a broken build once passed a flicker check.
This looks for the things a real room must have: a substantial amount of
non-black terrain in the playfield, and a non-empty HUD strip at the bottom.

Usage: python render-check.py <snapshot.png> [more.png ...]
The PNG is 1024x256 for a 256x256 logical screen (4x horizontal, 1x vertical).
"""
import sys
from PIL import Image

HUD_TOP = 208 * 1          # logical y 208 -> PNG row 208
FAIL = 0

for path in sys.argv[1:]:
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()

    play_lit = 0
    hud_lit = 0
    for y in range(h):
        for x in range(w):
            if px[x, y] != (0, 0, 0):
                if y >= HUD_TOP:
                    hud_lit += 1
                else:
                    play_lit += 1

    ok = play_lit >= 5000 and hud_lit >= 500
    print(f"{path}: playfield lit={play_lit} hud lit={hud_lit} -> "
          f"{'OK' if ok else 'BLANK/BROKEN'}")
    if not ok:
        FAIL = 1

sys.exit(FAIL)
