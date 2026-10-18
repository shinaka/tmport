"""Drive the original game in DOSBox-X WITHOUT stealing focus or touching the real mouse/keyboard.

Everything is done with posted window messages:
  * keyboard: WM_KEYDOWN/WM_KEYUP (WM_SYSKEY* for Alt chords) posted to SDL's child window,
  * screenshots / save states / turbo: WM_COMMAND posted with DOSBox-X's own menu ids
    (looked up by menu text, so they survive DOSBox-X version changes).
Screenshots are DOSBox-X's internal captures: the exact emulated 640x480 frame, even if the
window is covered or parked off-screen. (Minimised windows drop input, so we park instead.)

Guest mouse input is NOT possible this way: DOSBox-X only feeds Windows 3.1's PS/2 mouse while
it has captured the host mouse. Use the game's keyboard controls.

Usage (python -P tmctl.py <cmd> ...):
  start [--park]             launch DOSBox-X with tools/dosbox_tmwin.conf, never activated;
                             --park keeps re-parking for 45 s (DOSBox-X re-centres the window
                             on guest video-mode changes while Windows boots)
  stop                       terminate the DOSBox-X instance
  park | unpark              move the window off-screen / back on-screen (no activation);
                             while parked, every other command re-parks it if it moved
  key K [K ...]              keys: enter esc space tab backspace up down left right f1..f12,
                             letters/digits, chords like alt+o or shift+tab, "text:Hello World".
                             Extra tokens (processed left to right, all focus-safe):
                               hold:K:MS     key/chord K down, wait MS milliseconds, K up
                                             (e.g. hold:right:1000 walks for ~10 game ticks)
                               down:K / up:K post only the key-down / key-up of K (K stays
                                             "held" for the guest until its up is posted)
                               wait:MS       sleep MS milliseconds
                               fast:TEXT     type TEXT with ~50 ms per key (plain text: ~240 ms)
                             Held keys: the game polls held-arrow flags set by WM_KEYDOWN and
                             cleared by WM_KEYUP once per 100 ms tick, so a posted down ... up
                             pair separated by the wanted time is a real "hold" (no repeated
                             presses needed). A plain "up" press (~80 ms) can fall between two
                             ticks and be ignored: use hold:up:250 for doors/gates. Example
                             jump to the right: key down:right wait:250 hold:up:150 wait:1300 up:right
  shot OUT.png               save an exact 640x480 frame to OUT.png
  save SLOT | load SLOT      DOSBox-X save state in slot 1..10 (written to
                             tools/dosbox-x/bin/x64/Release/save/N.sav, so slots survive restarts)
  state save NAME | state load NAME [--keep-users] | state list [SUBDIR]
                             named state library in extracted/captures/savestates/NAME.sav (NAME may
                             contain a sub-folder, e.g. round2/r2_title_p0). Works through slot 10:
                             save = save 10 + copy; load = copy into slot 10 + load 10. A state is
                             stored together with the guest's USERS.DAT (NAME.users) and load puts
                             that file back (unless --keep-users): the game keeps the signed-in
                             player's slot index in memory, so a state and a different USERS.DAT
                             do not mix (a later save would overwrite another player's slot).
  turbo on|off               fast-forward emulation
  rec start | rec stop OUT.avi
                             toggle DOSBox-X video recording (AVI + ZMBV, every emulated frame);
                             stop moves the new AVI to OUT.avi. Decode with tools/avi_frames.py.
  menu                       list DOSBox-X menu commands and ids

Profiles (--profile win|dos, default win; the Windows profile behaves exactly as before):
  win   Windows 3.1 + Win CD edition, tools/dosbox_tmwin.conf, states in extracted/captures/savestates,
        USERS.DAT = extracted/win31_c/TLCWIN/TMTWINCD/USERS.DAT
  dos   1990 DOS edition (SST.EXE), tools/dosbox_tmdos.conf. C: = extracted/dos_c (a writable COPY of
        TM-DOS, game in C:\\TM), states in extracted/captures/dos/savestates,
        USERS.DAT = extracted/dos_c/TM/USERS.DAT. Extra start options:
          --machine M    DOSBox-X machine type (default ega; tandy, cga, hercules, mcga, vgaonly,
                         svga_s3 ...), passed as -set machine=M
          --args "A"     command line for SST.EXE (written to C:\\RUN.BAT), e.g. "-E" (EGA),
                         "-T" (Tandy), "-C" (CGA), "-H" (Hercules), "-M" (MCGA), "-Sa" (AdLib sound)
          --set K=V      any further DOSBox-X option (repeatable), e.g. --set "cycles=fixed 3000"
  start writes the profile name to extracted/captures/dosbox.profile; later commands (state ...)
  use the running profile unless --profile is given. Example:
    python -P tools/tmctl.py start --profile dos --machine tandy --park
    python -P tools/tmctl.py key enter text:ann enter
    python -P tools/tmctl.py state save tandy/signin   (-> extracted/captures/dos/savestates/tandy/)
  Note: the 1990 game drops shifted keys (shift+a types nothing); type names in lower case, the
  game capitalises the first letter itself. Slots 1..9 of save/load N are shared by both profiles.
"""
import ctypes
import ctypes.wintypes as wt
import glob
import os
import shutil
import subprocess
import sys
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
DOSBOX = os.path.join(TOOLS, 'dosbox-x', 'bin', 'x64', 'Release', 'dosbox-x.exe')
CONF = os.path.join(TOOLS, 'dosbox_tmwin.conf')
CAPDIR = os.path.normpath(os.path.join(TOOLS, '..', 'extracted', 'captures'))
PIDFILE = os.path.join(CAPDIR, 'dosbox.pid')
PARKFILE = os.path.join(CAPDIR, 'dosbox.parked')
PROFILEFILE = os.path.join(CAPDIR, 'dosbox.profile')
PARK_POS = (-4000, 100)

# DOS edition profile (see "Profiles" in the docstring)
CONF_DOS = os.path.join(TOOLS, 'dosbox_tmdos.conf')
DOS_C = os.path.normpath(os.path.join(TOOLS, '..', 'extracted', 'dos_c'))
DOS_USERS_DAT = os.path.join(DOS_C, 'TM', 'USERS.DAT')
DOS_STATELIB = os.path.join(CAPDIR, 'dos', 'savestates')

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP, WM_COMMAND = 0x100, 0x101, 0x104, 0x105, 0x111

# name -> (virtual key, scancode, extended)
KEYS = {
    'esc': (0x1B, 0x01, 0), 'enter': (0x0D, 0x1C, 0), 'space': (0x20, 0x39, 0), 'tab': (0x09, 0x0F, 0),
    'backspace': (0x08, 0x0E, 0), 'shift': (0x10, 0x2A, 0), 'ctrl': (0x11, 0x1D, 0), 'alt': (0x12, 0x38, 0),
    'up': (0x26, 0x48, 1), 'down': (0x28, 0x50, 1), 'left': (0x25, 0x4B, 1), 'right': (0x27, 0x4D, 1),
    'home': (0x24, 0x47, 1), 'end': (0x23, 0x4F, 1), 'pgup': (0x21, 0x49, 1), 'pgdn': (0x22, 0x51, 1),
    'insert': (0x2D, 0x52, 1), 'delete': (0x2E, 0x53, 1),
    '-': (0xBD, 0x0C, 0), '=': (0xBB, 0x0D, 0), ',': (0xBC, 0x33, 0), '.': (0xBE, 0x34, 0),
    '/': (0xBF, 0x35, 0), ';': (0xBA, 0x27, 0), "'": (0xDE, 0x28, 0),
}
for i, sc in enumerate([0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58]):
    KEYS[f'f{i + 1}'] = (0x70 + i, sc, 0)
for i, ch in enumerate('1234567890'):
    KEYS[ch] = (ord(ch), 0x02 + i, 0)
for row, base in (('qwertyuiop', 0x10), ('asdfghjkl', 0x1E), ('zxcvbnm', 0x2C)):
    for i, ch in enumerate(row):
        KEYS[ch] = (ord(ch.upper()), base + i, 0)


def _windows():
    out = []

    def cb(h, _):
        n = user32.GetWindowTextLengthW(h)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(h, buf, n + 1)
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(h, cls, 64)
        if cls.value == 'SDLParent' and buf.value.startswith('DOSBox-X'):
            out.append(h)
        return True

    user32.EnumWindows(EnumProc(cb), 0)
    return out


def find(timeout=0.0):
    t0 = time.time()
    while True:
        ws = _windows()
        if ws:
            top = ws[0]
            kids = []
            user32.EnumChildWindows(top, EnumProc(lambda h, _: kids.append(h) or True), 0)
            return top, (kids[0] if kids else top)
        if time.time() - t0 >= timeout:
            raise SystemExit('DOSBox-X window not found (is it running? use: tmctl.py start)')
        time.sleep(0.5)


def menu_items():
    top, _ = find()
    items = []

    def walk(m, path):
        for i in range(user32.GetMenuItemCount(m)):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetMenuStringW(m, i, buf, 256, 0x400)
            sub = user32.GetSubMenu(m, i)
            p = f'{path}/{buf.value.split(chr(9))[0]}'
            if sub:
                walk(sub, p)
            else:
                items.append((user32.GetMenuItemID(m, i), p))

    walk(user32.GetMenu(top), '')
    return items


def _find_item(needles):
    for mid, path in menu_items():
        if all(n.lower() in path.lower() for n in needles):
            return mid, path
    raise SystemExit(f'menu item not found: {needles}')


def close_popups():
    """Dismiss any extra top-level window DOSBox-X opened (dialogs must never linger on the
    user's screen). Returns the titles that were closed."""
    top, _ = find()
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(top, ctypes.byref(pid))
    closed = []

    def cb(h, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid.value and h != top and user32.IsWindowVisible(h):
            n = user32.GetWindowTextLengthW(h)
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(h, buf, n + 1)
            user32.PostMessageW(h, WM_COMMAND, 2, 0)  # IDCANCEL
            user32.PostMessageW(h, 0x0010, 0, 0)      # WM_CLOSE
            closed.append(buf.value)
        return True

    user32.EnumWindows(EnumProc(cb), 0)
    if closed:
        print('WARNING: dismissed DOSBox-X popup(s):', closed)
    return closed


# Note: DOSBox-X does not refresh check marks in its native Win32 menu, so toggles cannot be
# read reliably. Prompts are disabled in tools/dosbox_tmwin.conf (saveremark=false,
# forceloadstate=true) instead of by toggling menu items.


def menu_cmd(*needles, settle=0.6):
    """Post the first menu command whose path contains all needles (case-insensitive), then
    dismiss any popup it raised."""
    top, _ = find()
    mid, path = _find_item(needles)
    user32.PostMessageW(top, WM_COMMAND, mid, 0)
    time.sleep(settle)
    close_popups()
    return path


def _post_key(child, name, up, alt_down):
    vk, sc, ext = KEYS[name]
    lp = 1 | (sc << 16) | (ext << 24)
    if alt_down:
        lp |= 1 << 29
    if up:
        lp |= (1 << 30) | (1 << 31)
    sys_msg = alt_down or name == 'alt'
    msg = (WM_SYSKEYUP if up else WM_SYSKEYDOWN) if sys_msg else (WM_KEYUP if up else WM_KEYDOWN)
    user32.PostMessageW(child, msg, vk, lp)


def press(chord, hold=0.06, gap=0.12):
    _, child = find()
    parts = chord.lower().split('+')
    alt = 'alt' in parts
    for p in parts:
        _post_key(child, p, False, alt and p != 'alt')
        time.sleep(0.02)
    time.sleep(hold)
    for p in reversed(parts):
        _post_key(child, p, True, alt and p != 'alt')
        time.sleep(0.02)
    time.sleep(gap)


def type_text(text, hold=0.06, gap=0.12):
    for ch in text:
        if ch == ' ':
            press('space', hold, gap)
        elif ch.isupper():
            press('shift+' + ch.lower(), hold, gap)
        elif ch.lower() in KEYS:
            press(ch.lower(), hold, gap)
        else:
            raise SystemExit(f'cannot type {ch!r}')


def key_edge(chord, up):
    """Post only the key-down (up=False) or key-up (up=True) messages of a key or chord."""
    _, child = find()
    parts = chord.lower().split('+')
    alt = 'alt' in parts
    for p in (reversed(parts) if up else parts):
        _post_key(child, p, up, alt and p != 'alt')
        time.sleep(0.02)


def key_token(k):
    if k.startswith('text:'):
        type_text(k[5:])
    elif k.startswith('fast:'):
        type_text(k[5:], hold=0.005, gap=0.005)
    elif k.startswith('hold:'):
        chord, ms = k[5:].rsplit(':', 1)
        key_edge(chord, False)
        time.sleep(int(ms) / 1000.0)
        key_edge(chord, True)
    elif k.startswith('down:'):
        key_edge(k[5:], False)
    elif k.startswith('up:'):
        key_edge(k[3:], True)
    elif k.startswith('wait:'):
        time.sleep(int(k[5:]) / 1000.0)
    else:
        press(k)


def record(action, out=None, timeout=30.0):
    """Toggle DOSBox-X 'Record video to AVI'. On stop, move the newest AVI to out."""
    if action == 'start':
        menu_cmd('/Capture/', 'Record video')
        print('recording')
        return
    before = {p: os.path.getsize(p) for p in glob.glob(os.path.join(CAPDIR, '*.avi'))}
    menu_cmd('/Capture/', 'Record video')
    t0 = time.time()
    while time.time() - t0 < timeout:
        avis = glob.glob(os.path.join(CAPDIR, '*.avi'))
        if avis:
            src = max(avis, key=os.path.getmtime)
            s1 = os.path.getsize(src)
            time.sleep(0.5)
            if os.path.getsize(src) == s1 and s1 > 0:
                if out and os.path.abspath(src) != os.path.abspath(out):
                    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
                    shutil.move(src, out)
                    src = out
                print(src)
                return
        time.sleep(0.2)
    raise SystemExit('recording did not appear')


def shot(out, timeout=30.0):
    before = set(glob.glob(os.path.join(CAPDIR, '*.png')))
    menu_cmd('/Capture/', 'Take screenshot')
    t0 = time.time()
    while time.time() - t0 < timeout:
        new = [p for p in set(glob.glob(os.path.join(CAPDIR, '*.png'))) - before
               if os.path.getsize(p) > 0]
        if new:
            time.sleep(0.3)  # let DOSBox-X finish writing
            src = max(new, key=os.path.getmtime)
            if os.path.abspath(src) != os.path.abspath(out):
                os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
                shutil.move(src, out)
            print(out)
            return
        time.sleep(0.2)
    raise SystemExit('screenshot did not appear')


def set_pos(park):
    top, _ = find()
    SWP_NOSIZE, SWP_NOZORDER, SWP_NOACTIVATE = 0x1, 0x4, 0x10
    if park:
        x, y = PARK_POS
        open(PARKFILE, 'w').write('parked')
    else:
        x, y = 80, 80
        if os.path.exists(PARKFILE):
            os.remove(PARKFILE)
    user32.SetWindowPos(top, 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)


def ensure_parked():
    """DOSBox-X re-centres its window when the guest changes video mode (e.g. while Windows 3.1
    boots), which un-parks it. Re-park (without activation) whenever a park was requested."""
    if not os.path.exists(PARKFILE) or not _windows():
        return
    top, _ = find()
    r = wt.RECT()
    user32.GetWindowRect(top, ctypes.byref(r))
    if (r.left, r.top) != PARK_POS:
        set_pos(True)


def _dos_prepare(game_args):
    """DOS profile: check the writable game copy and write C:\\RUN.BAT for this start."""
    if not os.path.exists(os.path.join(DOS_C, 'TM', 'SST.EXE')):
        raise SystemExit(f'{DOS_C}\\TM is missing: copy TM-DOS\\* there first (never mount TM-DOS itself)')
    with open(os.path.join(DOS_C, 'RUN.BAT'), 'w', newline='\r\n') as f:
        f.write('@echo off\ncd \\TM\n')
        f.write(f'SST {game_args}'.rstrip() + '\n')


def start(park, profile='win', machine=None, game_args='', sets=()):
    if _windows():
        print('already running')
        return
    os.makedirs(CAPDIR, exist_ok=True)
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 4  # SW_SHOWNOACTIVATE: never take focus
    log = open(os.path.join(CAPDIR, 'dosbox.log'), 'w')
    if profile == 'dos':
        _dos_prepare(game_args)
        cmdline = [DOSBOX, '-conf', CONF_DOS]
        if machine:
            cmdline += ['-set', f'machine={machine}']
        for s in sets:
            cmdline += ['-set', s]
    else:
        cmdline = [DOSBOX, '-conf', CONF]
    p = subprocess.Popen(cmdline, cwd=os.path.dirname(DOSBOX), startupinfo=si,
                         stdout=log, stderr=subprocess.STDOUT)
    open(PIDFILE, 'w').write(str(p.pid))
    open(PROFILEFILE, 'w').write(profile)
    find(timeout=30)
    if park:
        set_pos(True)
        # keep it parked through the guest's video-mode switches during boot
        t0 = time.time()
        while time.time() - t0 < 45:
            time.sleep(0.5)
            ensure_parked()
    print(f'started pid {p.pid}')


def stop():
    try:
        pid = int(open(PIDFILE).read())
        subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
        os.remove(PIDFILE)
        print('stopped', pid)
    except (OSError, ValueError):
        subprocess.run(['taskkill', '/IM', 'dosbox-x.exe', '/F'], capture_output=True)
        print('stopped (by image name)')
    if os.path.exists(PROFILEFILE):
        os.remove(PROFILEFILE)


def running_profile():
    """Profile of the instance started by tmctl (written by start); 'win' if unknown."""
    try:
        p = open(PROFILEFILE).read().strip()
        return p if p in ('win', 'dos') else 'win'
    except OSError:
        return 'win'


STATELIB = os.path.join(CAPDIR, 'savestates')
USERS_DAT = os.path.normpath(os.path.join(TOOLS, '..', 'extracted', 'win31_c', 'TLCWIN', 'TMTWINCD',
                                          'USERS.DAT'))
STATE_SLOT = 10


def _slot_file(n):
    return os.path.join(os.path.dirname(DOSBOX), 'save', f'{n}.sav')


def _select_and(cmd, slot):
    menu_cmd('/Select save slot/', f'Slot {slot} ')
    time.sleep(0.3)
    return menu_cmd('/Capture/', 'Save state' if cmd == 'save' else 'Load state')


def state(args, profile='win'):
    """Named save states (see the docstring): state save NAME | load NAME [--keep-users] | list."""
    lib, users_dat = (DOS_STATELIB, DOS_USERS_DAT) if profile == 'dos' else (STATELIB, USERS_DAT)
    action = args[0] if args else 'list'
    if action == 'list':
        root = os.path.join(lib, args[1]) if len(args) > 1 else lib
        for dirpath, _, files in os.walk(root):
            for f in sorted(files):
                if f.endswith('.sav'):
                    p = os.path.join(dirpath, f)
                    print(os.path.relpath(p, lib)[:-4], time.ctime(os.path.getmtime(p)))
        return
    name = args[1]
    dst = os.path.join(lib, name + '.sav')
    slot = _slot_file(STATE_SLOT)
    if action == 'save':
        before = os.path.getmtime(slot) if os.path.exists(slot) else 0
        _select_and('save', STATE_SLOT)
        t0 = time.time()
        while time.time() - t0 < 20:
            if os.path.exists(slot) and os.path.getmtime(slot) > before:
                size = os.path.getsize(slot)
                time.sleep(0.5)
                if os.path.getsize(slot) == size:
                    break
            time.sleep(0.2)
        else:
            raise SystemExit('save state file was not written')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(slot, dst)
        if os.path.exists(users_dat):
            shutil.copy2(users_dat, dst[:-4] + '.users')
        print('saved', name)
    elif action == 'load':
        if not os.path.exists(dst):
            raise SystemExit(f'no such state: {dst}')
        shutil.copy2(dst, slot)
        users = dst[:-4] + '.users'
        if os.path.exists(users) and '--keep-users' not in args:
            shutil.copy2(users, users_dat)
            print('restored USERS.DAT of', name)
        _select_and('load', STATE_SLOT)
        print('loaded', name)
    else:
        print(__doc__)


def main(argv):
    if not argv:
        print(__doc__)
        return
    cmd, args = argv[0], argv[1:]
    # profile options (see "Profiles" in the docstring); without them nothing changes
    opts = {'--profile': None, '--machine': None, '--args': ''}
    sets, rest = [], []
    i = 0
    while i < len(args):
        if args[i] in opts and i + 1 < len(args):
            opts[args[i]] = args[i + 1]
            i += 2
        elif args[i] == '--set' and i + 1 < len(args):
            sets.append(args[i + 1])
            i += 2
        else:
            rest.append(args[i])
            i += 1
    args = rest
    profile = opts['--profile'] or ('win' if cmd == 'start' else running_profile())
    if profile not in ('win', 'dos'):
        raise SystemExit(f'unknown profile {profile!r} (win or dos)')
    if cmd not in ('start', 'stop', 'park', 'unpark'):
        ensure_parked()
    if cmd == 'start':
        start('--park' in args, profile, opts['--machine'], opts['--args'], sets)
    elif cmd == 'stop':
        stop()
    elif cmd in ('park', 'unpark'):
        set_pos(cmd == 'park')
    elif cmd == 'key':
        for k in args:
            key_token(k)
    elif cmd == 'shot':
        shot(args[0])
    elif cmd == 'rec':
        record(args[0], args[1] if len(args) > 1 else None)
    elif cmd in ('save', 'load'):
        slot = int(args[0])
        menu_cmd('/Select save slot/', f'Slot {slot} ')
        time.sleep(0.3)
        print(menu_cmd('/Capture/', 'Save state' if cmd == 'save' else 'Load state'))
    elif cmd == 'state':
        state(args, profile)
    elif cmd == 'turbo':
        print(menu_cmd('/CPU/', 'Turbo' if args[0] == 'on' else 'Normal speed'))
    elif cmd == 'menu':
        for mid, path in menu_items():
            print(mid, path)
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
