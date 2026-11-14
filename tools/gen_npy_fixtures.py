"""Write the .npy/.npz fixtures read by tests/io/test_npy.cpp and tests/pipeline/test_npz_values.cpp.

Run: python3 tools/gen_npy_fixtures.py tests/fixtures/npy  (numpy, pywt as pinned in tools/requirements.txt)
"""

import json
import sys
from pathlib import Path

import numpy as np


def save(out: Path, name: str, a: np.ndarray, expected: dict, **kw) -> None:
    np.save(out / name, a, **kw)
    record(expected, name, a)


def record(expected: dict, name: str, a: np.ndarray) -> None:
    flat = np.ravel(a, order="C")
    if a.dtype.kind == "U":
        data = [str(v) for v in flat]
    else:
        data = [float(v) for v in flat]
    expected[name] = {"shape": list(a.shape), "data": data}


def write_version(out: Path, name: str, a: np.ndarray, version: tuple, expected: dict) -> None:
    with open(out / name, "wb") as f:
        np.lib.format.write_array(f, a, version=version)
    record(expected, name, a)


def main() -> None:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    expected: dict = {}
    save(out, "f8.npy", np.array([[1.5, -2.25, 3.0], [0.0, 1e-300, -1e300]]), expected)
    save(out, "f4.npy", np.array([0.5, -1.25, 3.0], dtype=np.float32), expected)
    for bits in (8, 16, 32, 64):
        save(out, f"i{bits // 8}.npy", np.array([-3, 0, 7], dtype=f"int{bits}"), expected)
        top = 2 ** min(bits, 52) - 1
        save(out, f"u{bits // 8}.npy", np.array([0, 7, top], dtype=f"uint{bits}"), expected)
    save(out, "b1.npy", np.array([True, False, True]), expected)
    save(out, "be_f8.npy", np.array([1.0, -2.5, 3.25], dtype=">f8"), expected)
    save(out, "fortran.npy", np.asfortranarray(np.arange(6.0).reshape(2, 3)), expected)
    write_version(out, "v2.npy", np.arange(4.0), (2, 0), expected)
    write_version(out, "v3.npy", np.arange(3.0) - 1.5, (3, 0), expected)
    save(out, "unicode.npy", np.array(["сигнал", "ab"]), expected)
    np.save(out / "complex.npy", np.array([1 + 2j]))
    np.save(out / "object.npy", np.array([{"a": 1}], dtype=object), allow_pickle=True)

    a = np.arange(10.0).reshape(2, 5) / 4
    b = np.linspace(-1.0, 1.0, 7)
    name = np.array("db2")
    np.savez(out / "savez.npz", a=a, b=b, name=name)
    np.savez_compressed(out / "savez_compressed.npz", a=a, b=b, name=name)
    import zipfile

    with zipfile.ZipFile(out / "bad_member.npz", "w") as z:
        z.writestr("readme.txt", "not an array")
    header = "{'descr': '<f8', 'fortran_order': False, 'shape': (2, 200000000), }"
    header += " " * ((64 - (10 + len(header) + 1) % 64) % 64) + "\n"
    claim = b"\x93NUMPY\x01\x00" + len(header).to_bytes(2, "little") + header.encode() + bytes(64)
    with zipfile.ZipFile(out / "huge_claim.npz", "w") as z:
        z.writestr("cA1.npy", claim)
        z.writestr("cD1.npy", claim)
    np.savez_compressed(out / "zeros_compressed.npz", x=np.zeros(1 << 20))
    for key, value in (("a", a), ("b", b), ("name", name)):
        record(expected, "npz." + key, value)

    import pywt

    n = 1000
    t = np.arange(n) / n
    signal = np.sin(2 * np.pi * 5 * t) + 0.3 * np.sign(np.sin(2 * np.pi * 13 * t)) + 0.01 * np.arange(n) / n
    dwt = pywt.wavedec(signal, "db2", mode="symmetric", level=3)
    np.savez(out / "pywt_dwt.npz", signal=signal, wavelet=np.array("db2"), mode=np.array("symmetric"),
             **{k: v for k, v in zip(["cA3", "cD3", "cD2", "cD1"], dwt)})
    np.savez(out / "pywt_dwt_bare.npz", **{k: v for k, v in zip(["cA3", "cD3", "cD2", "cD1"], dwt)})
    m = 1024
    s2 = np.sin(2 * np.pi * 7 * np.arange(m) / m) + 0.2 * np.cos(2 * np.pi * 51 * np.arange(m) / m)
    swt = pywt.swt(s2, "db2", level=2, trim_approx=True)
    np.savez(out / "pywt_swt.npz", signal=s2, wavelet=np.array("db2"), **{k: v for k, v in zip(["cA2", "cD2", "cD1"], swt)})
    wp = pywt.WaveletPacket(signal, "db2", mode="symmetric", maxlevel=2)
    np.savez(out / "pywt_wpt.npz", signal=signal, wavelet=np.array("db2"), mode=np.array("symmetric"),
             **{node.path: node.data for node in wp.get_level(2, order="natural")})

    (out / "expected.json").write_text(json.dumps(expected, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
