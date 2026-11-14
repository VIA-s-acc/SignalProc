"""Run every generated bridge script under DIR in one process and report the ones that fail.

Usage: python3 -I tools/bridge/run_scripts.py DIR
"""

import contextlib
import io
import os
import runpy
import sys
import traceback
from pathlib import Path


def run_one(path: Path) -> tuple[bool, str]:
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            runpy.run_path(str(path), run_name="__main__")
        return True, out.getvalue()
    except SystemExit as e:
        return e.code in (0, None), out.getvalue()
    except Exception:
        return False, out.getvalue() + traceback.format_exc()


def main() -> int:
    root = Path(sys.argv[1]).resolve()
    os.chdir(root)
    scripts = sorted(p for p in root.rglob("*.py"))
    failures = []
    for p in scripts:
        ok, text = run_one(p)
        if not ok:
            failures.append((p, text))
    for p, text in failures:
        print(f"--- {p.relative_to(root)}\n{text}")
    print(f"scripts {len(scripts)}, failures {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
