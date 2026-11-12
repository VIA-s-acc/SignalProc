"""Write the DTCWT filter tables (src/core/dtcwt_tables.cpp) and golden vectors (tests/golden/dtcwt.json) from the Python `dtcwt` package."""

import json
import math
from pathlib import Path

import numpy as np

if not hasattr(np, "asfarray"):
    # dtcwt 0.13 still calls np.asfarray, which NumPy 2 removed; this is its documented behaviour.
    np.asfarray = lambda a, dtype=np.float64: np.asarray(a, dtype=dtype if np.issubdtype(np.dtype(dtype), np.inexact) else np.float64)

import dtcwt  # noqa: E402
import dtcwt.coeffs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BIORT = ["antonini", "legall", "near_sym_a", "near_sym_b"]
QSHIFT = ["qshift_06", "qshift_a", "qshift_b", "qshift_c", "qshift_d"]
BIORT_PARTS = ["h0o", "g0o", "h1o", "g1o"]
QSHIFT_PARTS = ["h0a", "h0b", "g0a", "g0b", "h1a", "h1b", "g1a", "g1b"]


def numbers(v):
    return ", ".join(f"{float(x):.17g}" for x in np.asarray(v).ravel())


def tables():
    out = ['#include "core/dtcwt.hpp"', "", "namespace signals::core::dtcwt {", "", "const std::vector<Biort>& biort_table() {",
           "    static const std::vector<Biort> table{"]
    for name in BIORT:
        parts = dtcwt.coeffs.biort(name)
        out.append(f'        {{"{name}", ' + ", ".join("{" + numbers(p) + "}" for p in parts) + "},")
    out += ["    };", "    return table;", "}", "", "const std::vector<Qshift>& qshift_table() {", "    static const std::vector<Qshift> table{"]
    for name in QSHIFT:
        parts = dtcwt.coeffs.qshift(name)
        out.append(f'        {{"{name}", ' + ", ".join("{" + numbers(p) + "}" for p in parts) + "},")
    out += ["    };", "    return table;", "}", "", "}  // namespace signals::core::dtcwt", ""]
    return "\n".join(out)


def signal(n, seed):
    return [math.sin(0.37 * k + seed) + 0.4 * math.cos(2.1 * k) + ((k * (seed + 3)) % 7 - 3) / 5 for k in range(n)]


def cases():
    out = []
    plan = [(16, 1, "near_sym_a", "qshift_a"), (64, 3, "near_sym_a", "qshift_a"), (100, 4, "near_sym_b", "qshift_b"), (54, 3, "antonini", "qshift_c"),
            (128, 5, "legall", "qshift_d"), (30, 2, "near_sym_a", "qshift_06"), (2, 1, "near_sym_a", "qshift_a")]
    for k, (n, levels, b, q) in enumerate(plan):
        x = np.array(signal(n, k))
        t = dtcwt.Transform1d(biort=b, qshift=q)
        p = t.forward(x, nlevels=levels)
        y = t.inverse(p)
        out.append({"n": n, "levels": levels, "biort": b, "qshift": q, "x": x.tolist(), "lowpass": p.lowpass.ravel().tolist(),
                    "highpasses": [{"re": h.real.ravel().tolist(), "im": h.imag.ravel().tolist()} for h in p.highpasses],
                    "inverse": np.asarray(y).ravel().tolist()})
    return out


def main():
    (ROOT / "src" / "core" / "dtcwt_tables.cpp").write_text(tables())
    (ROOT / "tests" / "golden" / "dtcwt.json").write_text(json.dumps({"version": dtcwt.__version__, "cases": cases()}, indent=1) + "\n")


if __name__ == "__main__":
    main()
