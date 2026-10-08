import ctypes, sys
from ctypes import wintypes
from PIL import Image

# Usage: shot.py [out.png] [window-title-substring]
# The title defaults to the Djupdal emulator; pass "MAME" to capture the MAME
# window instead. MAME can also snapshot its own framebuffer exactly -- see
# tools\mame.ps1 -Shot -- which avoids window scaling and is preferred for
# comparing pixels.
out = sys.argv[1] if len(sys.argv) > 1 else "shot.png"
title = sys.argv[2] if len(sys.argv) > 2 else "TIKI-100 Emulator"

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

EnumWindows = user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_int, ctypes.POINTER(ctypes.c_int))
found = []
def _cb(h, l):
    n = user32.GetWindowTextLengthW(h)
    if n:
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(h, buf, n + 1)
        if title in buf.value:
            found.append(h)
    return True
EnumWindows(EnumWindowsProc(_cb), 0)
if not found:
    print("NO WINDOW"); sys.exit(1)
hwnd = found[0]

rect = wintypes.RECT()
user32.GetClientRect(hwnd, ctypes.byref(rect))
w, h = rect.right, rect.bottom

hdc = user32.GetDC(hwnd)
memdc = gdi32.CreateCompatibleDC(hdc)
bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
gdi32.SelectObject(memdc, bmp)
# PW_RENDERFULLCONTENT = 2
user32.PrintWindow(hwnd, memdc, 2)

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]

bmi = BITMAPINFOHEADER()
bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
bmi.biWidth = w
bmi.biHeight = -h
bmi.biPlanes = 1
bmi.biBitCount = 32
bmi.biCompression = 0
buf = ctypes.create_string_buffer(w * h * 4)
gdi32.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bmi), 0)
img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)
out = sys.argv[1] if len(sys.argv) > 1 else "shot.png"
img.convert("RGB").save(out)
print("SAVED", out, w, h)
gdi32.DeleteObject(bmp); gdi32.DeleteDC(memdc); user32.ReleaseDC(hwnd, hdc)
