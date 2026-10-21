"""Finds the MischiefEngine checkout for this port's tools (the engine's study tools, tmctl.py, the
Ghidra scripts, live in its tools/ folder; docs/BUILDING.md "The engine").

    MISCHIEF_ENGINE_DIR when it is set, else ../MischiefEngine next to this repository

(the same order as the build's local override, cmake/engine.cmake). A build that downloaded the
pinned engine has no checkout for the tools: clone it next to the repository or set the variable.

From Python:  import mischief_engine; mischief_engine.add_tools_path(); import tmctl
From a shell: ENGINE="$(python -P tools/mischief_engine.py)"          the engine's folder
              python -P "$(python -P tools/mischief_engine.py --tools)/tmctl.py" ...
"""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def engine_dir():
    """The engine's folder; exits with a message when there is none."""
    d = os.environ.get('MISCHIEF_ENGINE_DIR') or os.path.join(os.path.dirname(REPO), 'MischiefEngine')
    d = os.path.normpath(d)
    if not os.path.isfile(os.path.join(d, 'tools', 'tmctl.py')):
        raise SystemExit(f'MischiefEngine not found at {d}: set MISCHIEF_ENGINE_DIR or clone it next to {REPO}')
    return d


def tools_dir():
    return os.path.join(engine_dir(), 'tools')


def add_tools_path():
    """Makes the engine's tools importable (import tmctl, mouse_tsr, avi_frames, ...)."""
    t = tools_dir()
    if t not in sys.path:
        sys.path.insert(0, t)
    return t


if __name__ == '__main__':
    print(tools_dir() if '--tools' in sys.argv[1:] else engine_dir())
