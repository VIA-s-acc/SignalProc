"""Run every generated bridge script and notebook under DIR in one process and report the ones that fail.

A notebook runs as the code of its cells in order, with matplotlib on the Agg backend.

Usage: python3 -I tools/bridge/run_scripts.py DIR
"""

import contextlib
import io
import json
import os
import runpy
import sys
import traceback
import warnings
from pathlib import Path

os.environ["MPLBACKEND"] = "Agg"
warnings.filterwarnings("ignore", message=".*non-interactive.*")


def notebook_code(path: Path) -> str:
    cells = json.loads(path.read_text(encoding="utf-8"))["cells"]
    return "\n\n".join("".join(c["source"]) for c in cells if c["cell_type"] == "code")


def run_one(path: Path) -> tuple[bool, str]:
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            if path.suffix == ".ipynb":
                exec(compile(notebook_code(path), str(path), "exec"), {"__name__": "__main__"})
            else:
                runpy.run_path(str(path), run_name="__main__")
        return True, out.getvalue()
    except SystemExit as e:
        return e.code in (0, None), out.getvalue()
    except Exception:
        return False, out.getvalue() + traceback.format_exc()
    finally:
        if "matplotlib.pyplot" in sys.modules:
            sys.modules["matplotlib.pyplot"].close("all")


def main() -> int:
    root = Path(sys.argv[1]).resolve()
    os.chdir(root)
    scripts = sorted([*root.rglob("*.py"), *root.rglob("*.ipynb")])
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
