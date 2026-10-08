import ctypes, os, sys, time
from ctypes import wintypes

# Window to drive. Defaults to the Djupdal emulator; set TIKI_WINDOW_TITLE to
# "MAME" to drive the MAME window instead.
TITLE = os.environ.get("TIKI_WINDOW_TITLE", "TIKI-100 Emulator")

user32 = ctypes.windll.user32
EnumWindows = user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_int, ctypes.POINTER(ctypes.c_int))
found = []
def _cb(h, l):
    n = user32.GetWindowTextLengthW(h)
    if n:
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(h, buf, n + 1)
        if TITLE in buf.value:
            found.append(h)
    return True
EnumWindows(EnumWindowsProc(_cb), 0)
if not found:
    print("NO WINDOW"); sys.exit(1)
hwnd = found[0]

# NB: posting WM_KEYDOWN/WM_KEYUP straight onto this window's message queue
# (which would avoid stealing the focus) does not work -- MAME discards
# keyboard input unless its window actually has the focus, with every keyboard
# provider. The MAME path therefore avoids host input altogether rather than
# trying to deliver it quietly; see tools\mame.ps1.
user32.SetForegroundWindow(hwnd)
time.sleep(0.3)

def click(cx, cy):
    # client coords -> screen
    pt = wintypes.POINT(cx, cy)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    user32.SetCursorPos(pt.x, pt.y)
    time.sleep(0.05)
    user32.mouse_event(0x0002, 0, 0, 0, 0)  # left down
    user32.mouse_event(0x0004, 0, 0, 0, 0)  # left up
    time.sleep(0.1)

def key(vk):
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.03)
    user32.keybd_event(vk, 0, 2, 0)
    time.sleep(0.03)

def typestr(s):
    for ch in s:
        if ch == '\n':
            key(0x0D)
        elif ch == ' ':
            key(0x20)
        else:
            vk = user32.VkKeyScanW(ord(ch)) & 0xFF
            key(vk)

cmd = sys.argv[1] if len(sys.argv) > 1 else ""
if cmd == "click":
    click(int(sys.argv[2]), int(sys.argv[3]))
elif cmd == "type":
    click(256, 200)  # focus display
    time.sleep(0.2)
    typestr(sys.argv[2])
elif cmd == "key":
    click(256, 200)
    time.sleep(0.2)
    for k in sys.argv[2:]:
        key(int(k, 0))
print("OK", cmd)
