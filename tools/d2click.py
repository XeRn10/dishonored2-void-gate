"""Click inside the Dishonored 2 window at screenshot coordinates (full-size client pixels): py d2click.py x y"""
import ctypes
import sys
import time
from ctypes import wintypes

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from d2ctl import find_game_window, focus_game  # noqa: E402

u = ctypes.windll.user32


def click(x, y, hold=0.08):
    hwnd = find_game_window()
    focus_game()
    pt = wintypes.POINT(int(x), int(y))
    u.ClientToScreen(hwnd, ctypes.byref(pt))
    u.SetCursorPos(pt.x, pt.y)
    time.sleep(0.15)
    u.mouse_event(0x2, 0, 0, 0, 0)
    time.sleep(hold)
    u.mouse_event(0x4, 0, 0, 0, 0)


if __name__ == "__main__":
    click(float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3]) if len(sys.argv) > 3 else 0.08)
