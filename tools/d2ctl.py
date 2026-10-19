r"""Talk to the Void Gate module inside a running Dishonored 2 (the module's test channel), and look at the game.

  py d2ctl.py <command> [args]      send one command, print the JSON reply
  py d2ctl.py shot [file.png]       screenshot of the game window (default: scratch\d2shot.png)
  py d2ctl.py press <vk>            focus the game and press a key (0x20 space: past "press to start")

Commands (module side, see vg.c "test channel"):
  ping                     module alive? player present? (answered also in menus)
  state                    player feet/eye/fwd, vertical speed, fall height, fall protection, gates
  cmds                     the engine's registered console commands
  npcs <radius>            NPCs around the player: id, position, entity/render flags
  log <text>               mark the module log (dmod_d2.log)
  console <text>           append text to the engine's command buffer (runs in the engine's next frame)
  tp x y z [yaw pitch]     teleport the player (feet position)          -- game frame
  gate i x y z nx ny nz    place gate i (0 = A, 1 = B) at a point with a normal  -- game frame
  clear                    close both gates                              -- game frame
  vz <m/s>                 set the player's vertical speed               -- game frame
  npcmode <0|1|2>          how NPCs are moved through gates              -- game frame

As a library: `from d2ctl import Game; g = Game(); g.cmd("state")` -> dict; g.shot(path).
"""
import ctypes
import ctypes.wintypes as wt
import itertools
import json
import os
import sys
import time

GAME_DIR = r"D:\Dishonored 2"
CMD = os.path.join(GAME_DIR, "dmod_cmd.txt")
REPLY = os.path.join(GAME_DIR, "dmod_reply.txt")
SCRATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scratch")


class Game:
    def __init__(self):
        self._ids = itertools.count(int(time.time() * 10) % 1000000)
        self._pos = os.path.getsize(REPLY) if os.path.exists(REPLY) else 0

    def send(self, line):
        """Queue one command; returns its id."""
        i = next(self._ids)
        for _ in range(200):  # the module deletes the file once read
            if not os.path.exists(CMD):
                break
            time.sleep(0.02)
        tmp = CMD + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(f"{i} {line}\n")
        os.replace(tmp, CMD)
        return i

    def wait(self, i, timeout=5.0):
        end = time.time() + timeout
        prefix = f"{i} "
        while time.time() < end:
            if os.path.exists(REPLY):
                size = os.path.getsize(REPLY)
                if size < self._pos:
                    self._pos = 0  # truncated
                if size > self._pos:
                    with open(REPLY, "rb") as f:
                        f.seek(self._pos)
                        data = f.read().decode("utf-8", "replace")
                    for ln in data.splitlines():
                        if ln.startswith(prefix):
                            return json.loads(ln[len(prefix):])
            time.sleep(0.03)
        raise TimeoutError(f"no reply to command {i} in {timeout} s (game not running, or no game frame?)")

    def cmd(self, line, timeout=5.0):
        return self.wait(self.send(line), timeout)

    # ---- screenshots of the game window (it must be visible: not minimized, not covered)
    def shot(self, path=None, scale=1.0):
        from PIL import ImageGrab
        hwnd = find_game_window()
        if not hwnd:
            raise RuntimeError("no Dishonored 2 window")
        r = wt.RECT()
        ctypes.windll.user32.GetClientRect(hwnd, ctypes.byref(r))
        pt = wt.POINT(0, 0)
        ctypes.windll.user32.ClientToScreen(hwnd, ctypes.byref(pt))
        img = ImageGrab.grab(bbox=(pt.x, pt.y, pt.x + r.right, pt.y + r.bottom), all_screens=True)
        if scale != 1.0:
            img = img.resize((int(img.width * scale), int(img.height * scale)))
        path = path or os.path.join(SCRATCH, "d2shot.png")
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        img.save(path)
        return os.path.abspath(path)


def press(vk, hold=0.08):
    """Bring the game window to the front and press a key (virtual-key code), e.g. past "press to start"."""
    user32 = ctypes.windll.user32
    hwnd = find_game_window()
    if not hwnd:
        raise RuntimeError("no Dishonored 2 window")
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    scan = user32.MapVirtualKeyW(vk, 0)
    user32.keybd_event(vk, scan, 0, 0)
    time.sleep(hold)
    user32.keybd_event(vk, scan, 2, 0)  # KEYEVENTF_KEYUP


def mouse(button, down):
    """Right/left mouse button down or up in the focused game (button: 'left' | 'right')."""
    flags = {("left", True): 0x2, ("left", False): 0x4, ("right", True): 0x8, ("right", False): 0x10}
    ctypes.windll.user32.mouse_event(flags[(button, down)], 0, 0, 0, 0)


def focus_game():
    user32 = ctypes.windll.user32
    hwnd = find_game_window()
    if hwnd:
        user32.ShowWindow(hwnd, 9)
        user32.SetForegroundWindow(hwnd)
        time.sleep(0.3)
    return hwnd


def find_game_window():
    user32 = ctypes.windll.user32
    found = []
    pid_of = wt.DWORD()

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if user32.IsWindowVisible(h) and buf.value.startswith("Dishonored"):
            user32.GetWindowThreadProcessId(h, ctypes.byref(pid_of))
            found.append(h)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    g = Game()
    if sys.argv[1] == "shot":
        print(g.shot(sys.argv[2] if len(sys.argv) > 2 else None))
        return
    if sys.argv[1] == "press":  # press <vk code, decimal or 0x..>
        press(int(sys.argv[2], 0))
        return
    print(json.dumps(g.cmd(" ".join(sys.argv[1:])), ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
