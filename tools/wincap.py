"""Capture (and later drive) a host window, e.g. DOSBox-X running the original game.

Usage:
  python -P wincap.py capture OUT.png [--title DOSBox-X] [--settle SECONDS] [--timeout SECONDS]
  python -P wincap.py key TEXT_OR_VK...        (send keystrokes to the window)
  python -P wincap.py click X Y                (click at client coords; requires host-integrated mouse)

--settle waits until the window content has been unchanged for SECONDS before capturing.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import sys
import time

from PIL import ImageChops, ImageGrab

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def find_window(title_sub):
    found = []

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if title_sub.lower() in buf.value.lower():
            found.append((hwnd, buf.value))
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found[0] if found else (None, None)


def client_bbox(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return (pt.x, pt.y, pt.x + r.right, pt.y + r.bottom)


def grab(hwnd):
    return ImageGrab.grab(bbox=client_bbox(hwnd), all_screens=True)


def capture(out, title, settle, timeout):
    t0 = time.time()
    hwnd = None
    while time.time() - t0 < timeout:
        hwnd, name = find_window(title)
        if hwnd:
            break
        time.sleep(0.5)
    if not hwnd:
        print('window not found')
        return 2
    last = grab(hwnd)
    stable_since = time.time()
    while time.time() - t0 < timeout:
        time.sleep(1.0)
        cur = grab(hwnd)
        if cur.size != last.size or ImageChops.difference(cur, last).getbbox():
            stable_since = time.time()
            last = cur
        elif time.time() - stable_since >= settle:
            break
    last.save(out)
    print(f'saved {out} {last.size} from "{name}" after {time.time() - t0:.1f}s')
    return 0


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wt.WORD), ('wScan', wt.WORD), ('dwFlags', wt.DWORD),
                ('time', wt.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wt.LONG), ('dy', wt.LONG), ('mouseData', wt.DWORD), ('dwFlags', wt.DWORD),
                ('time', wt.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class _U(ctypes.Union):
    _fields_ = [('ki', KEYBDINPUT), ('mi', MOUSEINPUT), ('pad', ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [('type', wt.DWORD), ('u', _U)]


KEYEVENTF_EXTENDEDKEY = 0x1
KEYEVENTF_KEYUP = 0x2
KEYEVENTF_SCANCODE = 0x8
# name -> (scancode, extended)
SCAN = {'esc': (0x01, 0), 'enter': (0x1C, 0), 'space': (0x39, 0), 'tab': (0x0F, 0),
        'backspace': (0x0E, 0), 'alt': (0x38, 0), 'ctrl': (0x1D, 0), 'shift': (0x2A, 0),
        'up': (0x48, 1), 'down': (0x50, 1), 'left': (0x4B, 1), 'right': (0x4D, 1),
        'f1': (0x3B, 0), 'f2': (0x3C, 0), 'f3': (0x3D, 0), 'f4': (0x3E, 0), 'f10': (0x44, 0)}
for i, ch in enumerate('1234567890'):
    SCAN[ch] = (0x02 + i, 0)
for row, base in (('qwertyuiop', 0x10), ('asdfghjkl', 0x1E), ('zxcvbnm', 0x2C)):
    for i, ch in enumerate(row):
        SCAN[ch] = (base + i, 0)


def _send_scan(sc, ext, up):
    flags = KEYEVENTF_SCANCODE | (KEYEVENTF_EXTENDEDKEY if ext else 0) | (KEYEVENTF_KEYUP if up else 0)
    inp = INPUT(type=1, u=_U(ki=KEYBDINPUT(0, sc, flags, 0, 0)))
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def focus(hwnd):
    """Bring hwnd to the foreground (tapping Alt first defeats the foreground lock)."""
    user32.ShowWindow(hwnd, 9)
    for _ in range(5):
        user32.keybd_event(0x12, 0, 0, 0)
        user32.keybd_event(0x12, 0, 2, 0)
        user32.SetForegroundWindow(hwnd)
        user32.BringWindowToTop(hwnd)
        time.sleep(0.3)
        if user32.GetForegroundWindow() == hwnd:
            return True
    print('warning: could not focus window')
    return False


def send_keys(hwnd, keys, hold=0.08, gap=0.15):
    """keys: list of names; 'alt+f' style chords supported."""
    focus(hwnd)
    for k in keys:
        parts = [SCAN[p] for p in k.lower().split('+')]
        for sc, ext in parts:
            _send_scan(sc, ext, False)
            time.sleep(0.02)
        time.sleep(hold)
        for sc, ext in reversed(parts):
            _send_scan(sc, ext, True)
            time.sleep(0.02)
        time.sleep(gap)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--title', default='DOSBox-X')
    ap.add_argument('--settle', type=float, default=5.0)
    ap.add_argument('--timeout', type=float, default=180.0)
    a = ap.parse_args()
    if a.cmd == 'capture':
        return capture(a.args[0], a.title, a.settle, a.timeout)
    if a.cmd == 'key':
        hwnd, _ = find_window(a.title)
        if not hwnd:
            print('window not found')
            return 2
        send_keys(hwnd, a.args)
        print('sent', a.args)
        return 0
    print('unknown command')
    return 1


if __name__ == '__main__':
    sys.exit(main())
