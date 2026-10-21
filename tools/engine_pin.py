"""The engine pin of this port: cmake/engine_pin.cmake against the local MischiefEngine checkout.

    python -P tools/engine_pin.py           print the pin, the checkout's HEAD and how they relate
    python -P tools/engine_pin.py --set     write the checkout's HEAD into cmake/engine_pin.cmake

The checkout is MISCHIEF_ENGINE_DIR, else ../MischiefEngine next to this repository (the build's
local override, cmake/engine.cmake). --set refuses a checkout with uncommitted changes (the pinned
commit would not hold them) unless --force; pin only commits that are pushed, or the pinned fetch
(-DMISCHIEF_ENGINE_FETCH=ON, CI, a fresh clone) cannot find them.
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIN = os.path.join(REPO, 'cmake', 'engine_pin.cmake')
PIN_RE = re.compile(r'set\(\s*MISCHIEF_ENGINE_GIT_TAG\s+([0-9a-fA-F]+)\s*\)')


def git(engine, *args):
    p = subprocess.run(['git', '-C', engine, *args], capture_output=True, text=True)
    return p.returncode, p.stdout.strip()


def main(argv):
    engine = os.path.normpath(os.environ.get('MISCHIEF_ENGINE_DIR') or os.path.join(os.path.dirname(REPO), 'MischiefEngine'))
    text = open(PIN, encoding='utf-8').read() if os.path.exists(PIN) else ''
    m = PIN_RE.search(text)
    pin = m.group(1) if m else None
    rc, head = git(engine, 'rev-parse', 'HEAD')
    if rc != 0:
        print(f'pin  {pin or "(none)"}  ({PIN})')
        raise SystemExit(f'no MischiefEngine checkout at {engine} (set MISCHIEF_ENGINE_DIR)')
    _, dirty = git(engine, 'status', '--porcelain', '--untracked-files=no')
    print(f'pin   {pin or "(none)"}  ({os.path.relpath(PIN, REPO)})')
    print(f'HEAD  {head}  ({engine}{", uncommitted changes" if dirty else ""})')
    if pin:
        rc_pin, full = git(engine, 'rev-parse', '--verify', '--quiet', pin + '^{commit}')
        if rc_pin != 0:
            print('the pinned commit is not in the checkout (fetch it, or the pin is wrong)')
        elif full == head:
            print('the pin is the checkout\'s HEAD')
        else:
            _, ahead = git(engine, 'rev-list', '--count', f'{full}..{head}')
            _, behind = git(engine, 'rev-list', '--count', f'{head}..{full}')
            print(f'HEAD is {ahead} commit(s) ahead of the pin and {behind} behind it')
    _, remotes = git(engine, 'branch', '-r', '--contains', head)
    if not remotes:
        print('HEAD is on no remote branch: push it before pinning it')
    if '--set' in argv:
        if dirty and '--force' not in argv:
            raise SystemExit('the checkout has uncommitted changes: commit them first (or --force)')
        line = f'set(MISCHIEF_ENGINE_GIT_TAG {head})'
        if m:
            text = text[:m.start()] + line + text[m.end():]
        else:
            text = ('# The MischiefEngine commit this port builds against (tools/engine_pin.py --set).\n'
                    + line + '\n')
        with open(PIN, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print(f'pinned {head}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
