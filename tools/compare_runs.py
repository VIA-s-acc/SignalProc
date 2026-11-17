"""Run graphs with an old and a new Signals and compare every probe bit for bit.

The old program runs the file as it was at a git revision (default v0.9.0), the new
program runs the file in the working tree, both with --npz.
Usage: compare_runs.py OLD_SIGNALS NEW_SIGNALS [--rev REV] FILE.sig...
"""

import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np


def run(binary: str, sig: Path, npz: Path) -> int:
    return subprocess.run([binary, "run", str(sig), "--npz", str(npz)], capture_output=True, cwd=sig.parent).returncode


def same(a: Path, b: Path) -> bool:
    with np.load(a, allow_pickle=False) as x, np.load(b, allow_pickle=False) as y:
        if sorted(x.files) != sorted(y.files):
            return False
        return all(x[k].dtype == y[k].dtype and x[k].shape == y[k].shape and x[k].tobytes() == y[k].tobytes() for k in x.files)


def main() -> int:
    args = sys.argv[1:]
    old, new, rev = args[0], args[1], "v0.9.0"
    files = args[2:]
    if files[:1] == ["--rev"]:
        rev, files = files[1], files[2:]
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, name in enumerate(files):
            path = Path(name)
            shown = subprocess.run(["git", "show", f"{rev}:{name}"], capture_output=True)
            if shown.returncode != 0:
                print(f"{name}: not in {rev}, skipped")
                continue
            before = path.parent / f".compare-{i}-{path.name}"
            before.write_bytes(shown.stdout)
            a, b = Path(tmp) / f"{i}-old.npz", Path(tmp) / f"{i}-new.npz"
            try:
                codes = (run(old, before.resolve(), a), run(new, path.resolve(), b))
            finally:
                before.unlink()
            if codes[0] != codes[1] or (a.exists() != b.exists()) or (a.exists() and not same(a, b)):
                print(f"{name}: DIFFERS (exit codes {codes[0]} and {codes[1]})")
                bad += 1
            else:
                print(f"{name}: same")
    print(f"{len(files)} graphs, {bad} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
