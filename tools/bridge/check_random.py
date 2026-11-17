"""Compare portable_random.py with draws the C++ generator wrote: check_random.py portable_random.py folder."""

import sys
from pathlib import Path

import numpy as np

source, folder = Path(sys.argv[1]), Path(sys.argv[2])
scope = {"np": np}
exec(source.read_text(encoding="utf-8"), scope)
draw = scope["portable_random"]

PINNED_NORMAL = [float.fromhex(h) for h in ("-0x1.42c3b2b72217p-5", "-0x1.8c1da014dda08p-2", "-0x1.fdd85e535a47ap-3", "0x1.5fa75918ca312p-1",
                                            "-0x1.bfaac17196979p-5", "-0x1.971d689089fdcp-1")]
PINNED_TPDF = [float.fromhex(h) for h in ("-0x1.4ba9e0cf612p-9", "0x1.b883e78806c2p-2", "-0x1.1ef49abf992e1p-1", "0x1.95d6c4f1d5aa2p-2")]

failures = []


def same(name, got, want):
    got = np.asarray(got, dtype=np.float64)
    want = np.asarray(want, dtype=np.float64)
    if got.shape != want.shape:
        failures.append(f"{name}: shape {got.shape} != {want.shape}")
        return
    differ = np.flatnonzero(got.view(np.uint64) != want.view(np.uint64))
    if differ.size:
        k = int(differ[0])
        failures.append(f"{name}: {differ.size} draws differ, first at {k}: {got[k]!r} != {want[k]!r}")


same("pinned normal, seed 1", draw(1, 6), PINNED_NORMAL)
same("pinned tpdf, seed 1", draw(1, 4, "tpdf"), PINNED_TPDF)
checked = 2
for file in sorted(folder.glob("*.npy")):
    kind, seed = file.stem.split("_")[:2]
    want = np.load(file)
    same(file.name, draw(int(seed), want.size, kind), want)
    checked += 1
print("\n".join(failures) if failures else f"{checked} draw sets equal bit for bit")
sys.exit(1 if failures else 0)
