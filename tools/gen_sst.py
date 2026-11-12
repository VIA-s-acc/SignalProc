"""Write golden vectors for the synchrosqueezed CWT (tests/golden/sst.json) from `ssqueezepy`."""

import json
import math
from pathlib import Path

import numpy as np
import ssqueezepy as sq
from ssqueezepy import Wavelet
from scipy import integrate
from ssqueezepy.utils.cwt_utils import adm_ssq

ROOT = Path(__file__).resolve().parent.parent


def signal(kind, n):
    k = np.arange(n, dtype=np.float64)
    if kind == "tones":
        return np.sin(0.37 * k) + 0.4 * np.cos(2.1 * k)
    if kind == "chirp":
        return np.cos(2 * math.pi * (0.02 * k + 0.0012 * k * k))
    return np.array([math.sin(0.9 * i + 1) + ((i * 5) % 7 - 3) / 5 for i in range(n)])


def admissibility(mu):
    cs = (1 + np.exp(-mu * mu) - 2 * np.exp(-0.75 * mu * mu)) ** -0.5
    c = np.sqrt(2) * cs * np.pi ** 0.25

    def f(w):
        if mu * w < 1:
            return c * math.exp(-0.5 * (w * w + mu * mu)) * math.expm1(mu * w) / w
        return c * (math.exp(-0.5 * (w - mu) ** 2) - math.exp(-0.5 * (w * w + mu * mu))) / w

    value, _ = integrate.quad(f, 0, mu + 40, limit=500, epsabs=1e-16, epsrel=1e-14)
    return value


def cases():
    out = []
    plan = [("tones", 150, 6.0, 2.0, 4, 17, "log", (0.01, 0.45), True), ("chirp", 128, 6.0, 1.5, 8, 30, "linear", (0.005, 0.5), False),
            ("mixed", 97, 13.4, 3.0, 2, 9, "log", (0.02, 0.4), False), ("mixed", 3, 6.0, 1.0, 1, 3, "log", (0.05, 0.4), False),
            ("tones", 64, 5.0, 1.0, 16, 48, "linear", (0.0, 0.5), False)]
    for kind, n, mu, s0, voices, count, spacing, (f_lo, f_hi), keep_wx in plan:
        x = signal(kind, n)
        w = Wavelet(("morlet", {"mu": mu, "dtype": "float64"}))
        scales = s0 * 2 ** (np.arange(count) / voices)
        freqs = np.geomspace(f_lo, f_hi, count) if spacing == "log" else np.linspace(f_lo, f_hi, count)
        tx, wx, *_ = sq.ssq_cwt(x, w, scales=scales, ssq_freqs=freqs, padtype="reflect")
        tx = tx[::-1]
        case = {"kind": kind, "n": n, "mu": mu, "s0": s0, "voices": voices, "count": count, "spacing": spacing, "freqs": freqs.tolist(),
                "x": x.tolist(), "tx_re": tx.real.ravel().tolist(), "tx_im": tx.imag.ravel().tolist(), "css": float(adm_ssq(w)), "css_exact": admissibility(mu),
                "inverse": sq.issq_cwt(tx[::-1], w).tolist()}
        if keep_wx:
            case["wx_re"] = wx.real.ravel().tolist()
            case["wx_im"] = wx.imag.ravel().tolist()
        out.append(case)
    return out


def main():
    (ROOT / "tests" / "golden" / "sst.json").write_text(json.dumps({"version": sq.__version__, "cases": cases()}, indent=1) + "\n")


if __name__ == "__main__":
    main()
