"""Checks that the shared engine core (portcore) depends on nothing but itself.

python -P tools/check_core_deps.py [--cmake cmake/portcore.cmake] [--gg] [-v]

The core's files are the ones listed in `add_library(portcore STATIC ...)` of cmake/portcore.cmake
(sources and headers), plus the test helpers named in its `set(PORTCORE_TEST_HELPERS ...)`. For
every one of them the script reads each `#include "..."` and resolves it the way the compiler does
(the file's own folder, then src/, then tests/ for the test helpers). It fails when an include
  * resolves to a repository file that is not in the core list (a Treasure Mountain or G&G header),
  * cannot be resolved in the repository and is not a known third-party header (ymfm), or
when a listed file is missing, or a core source is also compiled into tmcore (cmake/sources_*.cmake,
CMakeLists.txt), which would define its symbols twice.

--gg also checks the Gizmos & Gadgets code (src/gg/, tests/gg/): its includes must be core files or
G&G files (G&G links only portcore, re/gg/docs/SCOPING.md §9).

Exit code 0 when clean, 1 on a violation, 2 on a usage or parse error.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Headers of the third-party libraries the core is allowed to use (fetched by CMake).
EXTERNAL = {'ymfm_opl.h'}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, '/')


def cmake_list(text, opener):
    """The whitespace-separated items after `opener` up to the closing parenthesis (comments dropped)."""
    i = text.find(opener)
    if i < 0:
        return None
    items = []
    for line in text[i + len(opener):].splitlines():
        line = line.split('#', 1)[0]
        end = line.find(')')
        items.extend((line if end < 0 else line[:end]).split())
        if end >= 0:
            return items
    return None


def strip_comments(src):
    """C++ source without // and /* */ comments (strings are left alone; includes are not in them)."""
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    return re.sub(r'//[^\n]*', '', src)


def includes_of(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return INCLUDE_RE.findall(strip_comments(f.read()))


def resolve(name, including, search):
    """The repository file an #include "name" in `including` reaches, or None."""
    for base in [os.path.dirname(including)] + search:
        cand = os.path.normpath(os.path.join(base, name))
        if os.path.isfile(cand):
            return cand
    return None


def tm_sources():
    """Every file named in tmcore's lists (CMakeLists.txt and cmake/sources_*.cmake)."""
    out = set()
    files = [os.path.join(ROOT, 'CMakeLists.txt')]
    cdir = os.path.join(ROOT, 'cmake')
    files += [os.path.join(cdir, f) for f in sorted(os.listdir(cdir)) if f.startswith('sources_') and f.endswith('.cmake')]
    for p in files:
        with open(p, encoding='utf-8') as f:
            text = f.read()
        for opener in ('add_library(tmcore STATIC', 'target_sources(tmcore PRIVATE'):
            start = 0
            while True:
                i = text.find(opener, start)
                if i < 0:
                    break
                items = cmake_list(text[i:], opener) or []
                out.update(x for x in items if x.startswith('src/'))
                start = i + len(opener)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--cmake', default=os.path.join(ROOT, 'cmake', 'portcore.cmake'))
    ap.add_argument('--gg', action='store_true', help='also check src/gg and tests/gg against the core')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()

    try:
        with open(a.cmake, encoding='utf-8') as f:
            text = f.read()
    except OSError as e:
        print(f'check_core_deps: {e}', file=sys.stderr)
        return 2
    listed = cmake_list(text, 'add_library(portcore STATIC')
    if not listed:
        print(f'check_core_deps: no add_library(portcore STATIC ...) in {a.cmake}', file=sys.stderr)
        return 2
    helpers = cmake_list(text, 'set(PORTCORE_TEST_HELPERS') or []
    src = os.path.join(ROOT, 'src')
    tests = os.path.join(ROOT, 'tests')

    problems = []
    core = set()
    for item in listed + helpers:
        p = os.path.normpath(os.path.join(ROOT, item))
        if not os.path.isfile(p):
            problems.append(f'{item}: listed but missing')
        core.add(p)

    checked = 0
    for p in sorted(core):
        if not os.path.isfile(p):
            continue
        search = [src] + ([tests] if rel(p).startswith('tests/') else [])
        for name in includes_of(p):
            target = resolve(name, p, search)
            checked += 1
            if target is None:
                if os.path.basename(name) not in EXTERNAL:
                    problems.append(f'{rel(p)}: #include "{name}" is not a repository file nor a known third-party header')
                continue
            if target not in core:
                problems.append(f'{rel(p)}: #include "{name}" -> {rel(target)} is not in the core')
            elif a.verbose:
                print(f'  {rel(p)} -> {rel(target)}')

    dup = sorted(x for x in tm_sources() if os.path.normpath(os.path.join(ROOT, x)) in core)
    for x in dup:
        problems.append(f'{x}: in the core and in a tmcore source list (defined twice)')

    gg_checked = 0
    if a.gg:
        for top in (os.path.join(src, 'gg'), os.path.join(tests, 'gg')):
            for dirpath, _, names in os.walk(top):
                for n in sorted(names):
                    if not n.endswith(('.h', '.cpp', '.hpp', '.inl')):
                        continue
                    p = os.path.join(dirpath, n)
                    for name in includes_of(p):
                        target = resolve(name, p, [src, tests, os.path.join(tests, 'gg')])
                        gg_checked += 1
                        if target is None:
                            if os.path.basename(name) not in EXTERNAL:
                                problems.append(f'{rel(p)}: #include "{name}" is not a repository file')
                            continue
                        r = rel(target)
                        if target in core or r.startswith('src/gg/') or r.startswith('tests/gg/'):
                            continue
                        problems.append(f'{rel(p)}: #include "{name}" -> {r} is neither core nor G&G')

    for line in problems:
        print('FAIL ' + line)
    summary = f'{len(core)} core files ({len(helpers)} test helpers), {checked} includes checked'
    if a.gg:
        summary += f', {gg_checked} G&G includes checked'
    print(summary + (f': {len(problems)} problem(s)' if problems else ': clean'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
