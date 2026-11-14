"""Check that .npz files written by Signals open in numpy and restore with pywt.

Usage: python3 -I tools/bridge/check_files.py DIR  (DIR holds signal.npy, dwt.npz, swt.npz, wpt.npz, banks/*.npz, spec.json)
"""

import json
import sys
from pathlib import Path

import numpy as np
import pywt


def close(a, b, tol=1e-12):
    a, b = np.asarray(a, dtype=float), np.asarray(b, dtype=float)
    return a.shape == b.shape and float(np.max(np.abs(a - b))) <= tol * max(1.0, float(np.max(np.abs(b))))


def bands(f, prefix):
    keys = sorted((k for k in f.files if k.startswith(prefix)), key=lambda k: -int(k[2:]))
    return keys


def main() -> int:
    root = Path(sys.argv[1])
    spec = json.loads((root / "spec.json").read_text())
    x = np.load(root / "signal.npy", allow_pickle=False)
    failures = []
    f = np.load(root / "dwt.npz", allow_pickle=False)
    c = [f[k] for k in bands(f, "cA")] + [f[k] for k in bands(f, "cD")]
    y = pywt.waverec(c, str(f["wavelet"]), mode=str(f["mode"]), axis=-1)[..., : int(f["length"])]
    if not close(y, x) or str(f["detail_sign"]) != "pywt":
        failures.append("dwt.npz does not restore with pywt.waverec")
    if not close(pywt.wavedec(x, str(f["wavelet"]), mode=str(f["mode"]), level=len(c) - 1, axis=-1)[1], c[1]):
        failures.append("dwt.npz details are not in the pywt sign")
    f = np.load(root / "swt.npz", allow_pickle=False)
    c = [f[k] for k in bands(f, "cA")] + [f[k] for k in bands(f, "cD")]
    y = pywt.iswt(c, str(f["wavelet"]), norm=bool(f["norm"]), axis=-1)[..., : int(f["length"])]
    if not close(y, x):
        failures.append("swt.npz does not restore with pywt.iswt")
    f = np.load(root / "wpt.npz", allow_pickle=False)
    paths = [k for k in f.files if set(k) <= {"a", "d"}]
    for ch in range(x.shape[0]):
        wp = pywt.WaveletPacket(None, str(f["wavelet"]), mode=str(f["mode"]), maxlevel=len(paths[0]))
        for p in paths:
            wp[p] = f[p][ch]
        if not close(wp.reconstruct(update=False)[: int(f["length"])], x[ch]):
            failures.append(f"wpt.npz channel {ch} does not restore with pywt.WaveletPacket")
    for name in spec["by_name"] + spec["by_filter_bank"]:
        b = np.load(root / "banks" / f"{name}.npz", allow_pickle=False)
        ref = pywt.Wavelet(name).filter_bank
        err = max(float(np.max(np.abs(np.asarray(b[k]) - np.asarray(r)))) if len(b[k]) == len(r) else 1.0 for k, r in zip(("dec_lo", "dec_hi", "rec_lo", "rec_hi"), ref))
        if name in spec["by_name"] and err > 1e-15:
            failures.append(f"{name}: goes to pywt by name but its filters differ by {err:.1e}")
        if name in spec["by_filter_bank"] and err <= 1e-15:
            failures.append(f"{name}: is listed for filter_bank but equals pywt")
        if name in spec["by_filter_bank"] and err > 1e-9:
            failures.append(f"{name}: differs from pywt by {err:.1e}")
    for line in failures:
        print(line)
    print(f"file checks: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
