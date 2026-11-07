"""Generate PyWavelets golden vectors and wavelet tables for the C++ tests."""

import argparse
import json
import math
import struct
import warnings
from pathlib import Path

import numpy as np
import pywt
import scipy.io.wavfile
import scipy.signal
from skimage.restoration import _denoise

MODES = ["zero", "constant", "symmetric", "reflect", "periodic", "smooth",
         "periodization", "antisymmetric", "antireflect"]
REPRESENTATIVE = ["haar", "db2", "db4", "db10", "sym5", "coif3", "bior2.2", "bior4.4", "rbio3.1", "dmey"]
DWT_LENGTHS = [1, 2, 7, 8, 64, 65]
WAVEDEC_LENGTHS = [64, 65, 1000]
PAD_SOURCE = [1, 2, 4, 8, 16, 3, -5, 7]
PAD_LENGTHS = [1, 2, 3, 5, 8]
PAD_WIDTHS = [0, 1, 3, 7, 13]
TABLE_NAMES = ([f"coif{n}" for n in range(1, 18)] + ["dmey", "bior4.4", "bior5.5", "bior6.8"]
               + [f"sym{n}" for n in range(2, 21)])


def signal(n):
    return [math.sin(0.3 * k) + 0.5 * math.cos(1.7 * k) + ((k % 5) - 2) / 4 for k in range(n)]


def floats(a):
    return [float(v) for v in a]


def filters_json():
    wavelets = []
    for name in pywt.wavelist(kind="discrete"):
        w = pywt.Wavelet(name)
        wavelets.append({
            "name": name, "family": w.short_family_name, "orthogonal": bool(w.orthogonal),
            "biorthogonal": bool(w.biorthogonal), "symmetry": w.symmetry,
            "vanishing_moments_psi": w.vanishing_moments_psi, "vanishing_moments_phi": w.vanishing_moments_phi,
            "dec_lo": floats(w.dec_lo), "dec_hi": floats(w.dec_hi),
            "rec_lo": floats(w.rec_lo), "rec_hi": floats(w.rec_hi),
        })
    return {"pywt_version": pywt.__version__, "wavelets": wavelets}


def pad_json():
    cases = []
    for mode in MODES:
        for n in PAD_LENGTHS:
            if mode == "smooth" and n == 1:
                continue
            x = np.array(PAD_SOURCE[:n], dtype=float)
            for before in PAD_WIDTHS:
                for after in PAD_WIDTHS:
                    try:
                        y = pywt.pad(x, (before, after), mode)
                    except (ValueError, IndexError):
                        continue
                    cases.append({"mode": mode, "x": floats(x), "before": before, "after": after, "y": floats(y)})
    return {"cases": cases}


def dwt_json():
    lengths = sorted(set([37] + DWT_LENGTHS))
    signals = {str(n): signal(n) for n in lengths}
    cases = []
    for name in pywt.wavelist(kind="discrete"):
        for mode in MODES:
            ca, cd = pywt.dwt(signals["37"], name, mode=mode)
            cases.append({"wavelet": name, "mode": mode, "n": 37, "cA": floats(ca), "cD": floats(cd)})
    for name in REPRESENTATIVE:
        for mode in MODES:
            for n in DWT_LENGTHS:
                try:
                    ca, cd = pywt.dwt(signals[str(n)], name, mode=mode)
                except ValueError as e:
                    cases.append({"wavelet": name, "mode": mode, "n": n, "error": str(e)})
                    continue
                cases.append({"wavelet": name, "mode": mode, "n": n, "cA": floats(ca), "cD": floats(cd)})
    return {"signals": signals, "cases": cases}


def wavedec_json():
    signals = {str(n): signal(n) for n in WAVEDEC_LENGTHS}
    cases = []
    for name in REPRESENTATIVE:
        w = pywt.Wavelet(name)
        for mode in MODES:
            for n in WAVEDEC_LENGTHS:
                max_level = pywt.dwt_max_level(n, w.dec_len)
                levels = {max_level} if n == 1000 else {1, max_level}
                for level in sorted(levels):
                    if level < 1 or level > max_level:
                        continue
                    coeffs = pywt.wavedec(signals[str(n)], name, mode=mode, level=level)
                    case = {"wavelet": name, "mode": mode, "n": n, "level": level, "max_level": max_level,
                            "coeffs": [floats(c) for c in coeffs]}
                    if n != 1000:
                        case["waverec"] = floats(pywt.waverec(coeffs, name, mode=mode))
                    cases.append(case)
    return {"signals": signals, "cases": cases}


SWT_WAVELETS = ["haar", "db2", "db4", "sym8", "coif2", "bior2.2"]
SWT_LENGTHS = [64, 96]


def swt_json():
    signals = {str(n): signal(n) for n in SWT_LENGTHS}
    cases = []
    for name in SWT_WAVELETS:
        for n in SWT_LENGTHS:
            max_level = pywt.swt_max_level(n)
            for level in sorted({1, 3, max_level}):
                if level < 1 or level > max_level:
                    continue
                x = np.asarray(signals[str(n)])
                for norm in (False, True):
                    coeffs = pywt.swt(x, name, level=level, trim_approx=True, norm=norm)
                    case = {"wavelet": name, "n": n, "level": level, "max_level": max_level, "norm": norm,
                            "coeffs": [floats(c) for c in coeffs],
                            "iswt": floats(pywt.iswt(coeffs, name, norm=norm))}
                    if norm:
                        case["mra"] = [floats(c) for c in pywt.mra(x, name, level=level, transform="swt")]
                    cases.append(case)
    return {"signals": signals, "cases": cases}


PACKET_WAVELETS = ["haar", "db2", "sym4", "coif1", "bior2.2"]
PACKET_MODES = ["symmetric", "periodization", "zero"]
PACKET_LENGTHS = [64, 77]


def packets_json():
    signals = {str(n): signal(n) for n in PACKET_LENGTHS}
    cases = []
    for name in PACKET_WAVELETS:
        for mode in PACKET_MODES:
            for n in PACKET_LENGTHS:
                x = np.asarray(signals[str(n)])
                max_level = pywt.dwt_max_level(n, pywt.Wavelet(name).dec_len)
                for level in sorted({3, max_level}):
                    wp = pywt.WaveletPacket(x, name, mode=mode, maxlevel=level)
                    nodes = {node.path: floats(node.data) for j in range(1, level + 1) for node in wp.get_level(j, "natural")}
                    for child in ["aaa", "aad", "da", "dd"]:
                        del wp[child]
                    leaves = [node.path for node in wp.get_leaf_nodes(decompose=False)]
                    cases.append({"wavelet": name, "mode": mode, "n": n, "level": level, "nodes": nodes,
                                  "basis": leaves, "reconstruct": floats(wp.reconstruct(update=False))})
    return {"signals": signals, "cases": cases}


CWT_WAVELETS = ["morl", "mexh", "cmor1.5-1.0"]
CWT_LENGTHS = [100, 257]
CWT_SCALES = [0.5, 1.0, 2.5, 8.0, 40.0]


def cwt_json():
    signals = {str(n): signal(n) for n in CWT_LENGTHS}
    cases = []
    for name in CWT_WAVELETS:
        w = pywt.ContinuousWavelet(name)
        for n in CWT_LENGTHS:
            coefs, freqs = pywt.cwt(np.asarray(signals[str(n)]), CWT_SCALES, w, method="conv")
            cases.append({"wavelet": name, "n": n, "scales": CWT_SCALES,
                          "re": [floats(np.real(r)) for r in coefs], "im": [floats(np.imag(r)) for r in coefs],
                          "frequencies": floats(freqs), "center": float(pywt.scale2frequency(w, 1.0, 12))})
    return {"signals": signals, "cases": cases}

WAV_FRAMES = 64
PCM_SUBTYPE_GUID = bytes.fromhex("0100000000001000800000aa00389b71")
G711_SEG_END = [0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF]


def wav_tone(channels):
    k = np.arange(WAV_FRAMES)
    return np.stack([0.9 * np.sin(2 * np.pi * (3 + 2 * c) * k / WAV_FRAMES + 0.4 * c) for c in range(channels)], axis=1)


def quantize(x, bits):
    scale = 2 ** (bits - 1)
    codes = np.clip(np.round(x * scale), -scale, scale - 1).astype(np.int64)
    codes[0, 0], codes[1, 0] = -scale, scale - 1
    return codes


def segment(v):
    return next((i for i, end in enumerate(G711_SEG_END) if v <= end), 8)


def linear_to_ulaw(v):
    v, mask = (0x84 - v, 0x7F) if v < 0 else (v + 0x84, 0xFF)
    seg = segment(v)
    return (0x7F ^ mask) if seg >= 8 else (((seg << 4) | ((v >> (seg + 3)) & 0xF)) ^ mask)


def linear_to_alaw(v):
    v, mask = (v, 0xD5) if v >= 0 else (-v - 8, 0x55)
    seg = segment(v)
    if seg >= 8:
        return 0x7F ^ mask
    quant = (v >> 4) if seg < 2 else (v >> (seg + 3))
    return ((seg << 4) | (quant & 0xF)) ^ mask


def ulaw_to_linear(u):
    u = ~u & 0xFF
    t = (((u & 0xF) << 3) + 0x84) << ((u & 0x70) >> 4)
    return (0x84 - t) if u & 0x80 else (t - 0x84)


def alaw_to_linear(a):
    a ^= 0x55
    t = (a & 0xF) << 4
    seg = (a & 0x70) >> 4
    t = t + 8 if seg == 0 else (t + 0x108) << max(seg - 1, 0)
    return t if a & 0x80 else -t


def riff(fmt, data, extra_chunks=b""):
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + extra_chunks
    body += b"data" + struct.pack("<I", len(data)) + data + (b"\0" if len(data) % 2 else b"")
    return b"RIFF" + struct.pack("<I", len(body)) + body


def fmt_chunk(tag, channels, rate, bits, cb_size=None):
    block = channels * bits // 8
    fmt = struct.pack("<HHIIHH", tag, channels, rate, rate * block, block, bits)
    return fmt if cb_size is None else fmt + struct.pack("<H", cb_size)


def wav_case(file, fmt, rate, samples, extensible=False):
    return {"file": file, "format": fmt, "channels": len(samples), "sample_rate": rate,
            "frames": WAV_FRAMES, "extensible": extensible, "samples": [floats(ch) for ch in samples]}


def wav_json(fixtures):
    fixtures.mkdir(parents=True, exist_ok=True)
    cases = []

    u8 = quantize(wav_tone(1), 8) + 128
    scipy.io.wavfile.write(fixtures / "u8_mono.wav", 8000, u8[:, 0].astype(np.uint8))
    cases.append(wav_case("u8_mono.wav", "pcm_u8", 8000, ((u8.T - 128) / 128)))

    s16 = quantize(wav_tone(2), 16)
    scipy.io.wavfile.write(fixtures / "s16_stereo.wav", 44100, s16.astype(np.int16))
    cases.append(wav_case("s16_stereo.wav", "pcm_s16", 44100, s16.T / 32768))

    s24 = quantize(wav_tone(2), 24)
    data = b"".join(int(v & 0xFFFFFF).to_bytes(3, "little") for v in s24.reshape(-1))
    (fixtures / "s24_stereo.wav").write_bytes(riff(fmt_chunk(1, 2, 48000, 24), data))
    cases.append(wav_case("s24_stereo.wav", "pcm_s24", 48000, s24.T / 8388608))

    s32 = quantize(wav_tone(1), 32)
    scipy.io.wavfile.write(fixtures / "s32_mono.wav", 96000, s32[:, 0].astype(np.int32))
    cases.append(wav_case("s32_mono.wav", "pcm_s32", 96000, s32.T / 2147483648))

    f32 = wav_tone(2).astype(np.float32)
    f32[0, 0], f32[1, 0] = -1.0, 1.0
    scipy.io.wavfile.write(fixtures / "f32_stereo.wav", 44100, f32)
    cases.append(wav_case("f32_stereo.wav", "float32", 44100, f32.T.astype(np.float64)))

    f64 = wav_tone(1)
    f64[0, 0], f64[1, 0] = 1.0 / 3.0, -1.0
    scipy.io.wavfile.write(fixtures / "f64_mono.wav", 22050, f64[:, 0])
    cases.append(wav_case("f64_mono.wav", "float64", 22050, f64.T))

    s6 = quantize(wav_tone(6), 16)
    fmt = fmt_chunk(0xFFFE, 6, 48000, 16, 22) + struct.pack("<HI", 16, 0x3F) + PCM_SUBTYPE_GUID
    data = s6.astype("<i2").tobytes()
    (fixtures / "s16_6ch_extensible.wav").write_bytes(riff(fmt, data))
    cases.append(wav_case("s16_6ch_extensible.wav", "pcm_s16", 48000, s6.T / 32768, extensible=True))

    pcm = quantize(wav_tone(1), 16)[:, 0]
    for name, tag, encode, decode in [("mulaw", 7, linear_to_ulaw, ulaw_to_linear),
                                      ("alaw", 6, linear_to_alaw, alaw_to_linear)]:
        codes = bytes(encode(int(v)) for v in pcm)
        decoded = np.array([decode(c) for c in codes], dtype=float)
        assert np.max(np.abs(decoded - pcm)) <= 1024, name
        fact = b"fact" + struct.pack("<II", 4, WAV_FRAMES)
        (fixtures / f"{name}_mono.wav").write_bytes(riff(fmt_chunk(tag, 1, 8000, 8, 0), codes, fact))
        cases.append(wav_case(f"{name}_mono.wav", name, 8000, [decoded / 32768]))

    (fixtures / "truncated.wav").write_bytes((fixtures / "s16_stereo.wav").read_bytes()[:20])
    (fixtures / "not_a_wav.wav").write_text("not a wav\n")
    (fixtures / "empty.wav").write_bytes(riff(fmt_chunk(1, 2, 44100, 16), b""))
    return {"cases": cases, "invalid": ["truncated.wav", "not_a_wav.wav", "empty.wav"]}


def signals_json():
    chirps = []
    for method, f0, f1, fs, n in [("linear", 100.0, 3000.0, 8000.0, 1000), ("logarithmic", 20.0, 4000.0, 16000.0, 2048)]:
        t = np.arange(n) / fs
        y = scipy.signal.chirp(t, f0, (n - 1) / fs, f1, method=method)
        chirps.append({"method": method, "f0": f0, "f1": f1, "fs": fs, "n": n, "y": floats(y)})
    dj = [{"name": name, "n": n, "y": floats(pywt.data.demo_signal(name, n))}
          for name in ["Blocks", "Bumps", "HeaviSine", "Doppler"] for n in [64, 1024]]
    return {"chirp": chirps, "dj": dj}


def kaiser_json():
    order = []
    for attenuation, width in [(60, 0.1), (80, 0.05), (40, 0.2)]:
        taps, beta = scipy.signal.kaiserord(attenuation, width)
        order.append({"attenuation": attenuation, "width": width, "taps": int(taps), "beta": float(beta)})
    beta = [{"attenuation": a, "beta": float(scipy.signal.kaiser_beta(a))} for a in [10, 21, 30, 50, 60, 90]]
    firwin = [{"taps": taps, "cutoff": cutoff, "beta": b,
               "h": floats(scipy.signal.firwin(taps, cutoff, window=("kaiser", b)))}
              for taps, cutoff, b in [(31, 0.25, 8.0), (63, 0.125, 7.857), (32, 0.5, 5.0)]]
    return {"kaiserord": order, "kaiser_beta": beta, "firwin": firwin}


def upfirdn_json():
    x = np.sin(2 * np.pi * 0.07 * np.arange(37))
    filters = {"firwin31": scipy.signal.firwin(31, 0.25), "ramp3": np.array([1.0, 2.0, 3.0])}
    cases = [{"filter": name, "h": floats(h), "x": floats(x), "up": up, "down": down,
              "y": floats(scipy.signal.upfirdn(h, x, up, down))}
             for name, h in filters.items() for up, down in [(1, 1), (1, 2), (1, 4), (3, 1), (2, 3)]]
    return {"cases": cases}


def freqz_json():
    filters = [("db2 dec_lo", pywt.Wavelet("db2").dec_lo), ("db2 dec_hi", pywt.Wavelet("db2").dec_hi),
               ("bior2.2 dec_lo", pywt.Wavelet("bior2.2").dec_lo), ("sym8 dec_lo", pywt.Wavelet("sym8").dec_lo),
               ("coif3 dec_lo", pywt.Wavelet("coif3").dec_lo), ("firwin31", scipy.signal.firwin(31, 0.25))]
    cases = []
    for name, b in filters:
        b = np.asarray(b, dtype=float)
        _, h = scipy.signal.freqz(b, worN=512)
        _, gd = scipy.signal.group_delay((b, 1), w=512)
        cases.append({"name": name, "b": floats(b), "h_re": floats(h.real), "h_im": floats(h.imag), "gd": floats(gd)})
    return {"points": 512, "cases": cases}


def threshold_json():
    x = np.array([-3, -1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2, 4], dtype=float)
    cases = [{"mode": mode, "value": value, "substitute": substitute, "x": floats(x),
              "y": floats(pywt.threshold(x, value, mode, substitute))}
             for mode in ["soft", "hard", "garrote", "greater", "less"] for value in [0.5, 1.5] for substitute in [0, -1]]
    cases.append({"mode": "firm", "low": 1.0, "high": 2.0, "x": floats(x), "y": floats(pywt.threshold_firm(x, 1, 2))})
    return {"cases": cases, "sure": sure_cases(), "minimax": [{"n": n, "lambda": minimaxi(n)} for n in [1, 16, 32, 33, 1000, 8192]]}


def rigrsure(x):
    n = x.size
    sx2 = np.sort(np.abs(x)) ** 2
    risks = (n - 2 * np.arange(1, n + 1) + (np.cumsum(sx2) + np.arange(n - 1, -1, -1) * sx2)) / n
    return float(np.sqrt(sx2[np.argmin(risks)]))


def heursure(x):
    n = x.size
    universal = float(np.sqrt(2 * np.log(n)))
    eta = (np.linalg.norm(x) ** 2 - n) / n
    crit = np.log2(n) ** 1.5 / np.sqrt(n)
    return universal if eta < crit else min(rigrsure(x), universal)


def minimaxi(n):
    return 0.0 if n <= 32 else float(0.3936 + 0.1829 * np.log2(n))


def sure_cases():
    rng = np.random.default_rng(3)
    k = np.arange(512)
    spikes = np.zeros(512)
    spikes[[17, 99, 300, 301, 450]] = [2.0, -1.5, 0.8, 0.9, -3.0]
    bands = {"dense": 0.1 * rng.normal(0, 1, 512) + 0.3 * np.sin(0.05 * k),
             "noise": 0.05 * rng.normal(0, 1, 512),
             "sparse": spikes + 0.01 * rng.normal(0, 1, 512),
             "short": 0.2 * rng.normal(0, 1, 20) + np.linspace(-1, 1, 20),
             "signal": np.sin(0.2 * k) + 0.02 * rng.normal(0, 1, 512)}
    sigmas = {"dense": 0.1, "noise": 0.05, "sparse": 0.01, "short": 0.2, "signal": 0.02}
    return [{"name": name, "sigma": sigmas[name], "x": floats(x), "sure": sigmas[name] * heursure(x / sigmas[name]),
             "rigrsure": sigmas[name] * rigrsure(x / sigmas[name])} for name, x in bands.items()]


def denoise_signals(sample_wav):
    _, data = scipy.io.wavfile.read(sample_wav)
    music = data[100_000:102_048, 0].astype(float) / 32768
    sigma = math.sqrt(np.mean(music ** 2) / 10 ** (10 / 10))
    doppler = pywt.data.demo_signal("Doppler", 1024)
    doppler = doppler * 7 / np.std(doppler)
    return {"music": (music, music + np.random.default_rng(7).normal(0, sigma, music.size)),
            "doppler": (doppler, doppler + np.random.default_rng(3).normal(0, 1, doppler.size))}


def denoise_json(sample_wav):
    signals = denoise_signals(sample_wav)
    combos = [(w, m, t, 5) for w in ["sym8", "db4", "haar"] for m in ["BayesShrink", "VisuShrink"] for t in ["soft", "hard"]]
    combos.append(("sym8", "BayesShrink", "soft", None))
    cases = []
    for name, (_, noisy) in signals.items():
        for wavelet, method, mode, levels in combos:
            d1 = pywt.wavedec(noisy, wavelet, mode="symmetric", level=1)[-1]
            out = _denoise._wavelet_threshold(noisy, wavelet, method=method, mode=mode, wavelet_levels=levels)
            cases.append({"signal": name, "wavelet": wavelet, "method": method, "mode": mode, "levels": levels,
                          "sigma": float(_denoise._sigma_est_dwt(d1)), "out": floats(out)})
    return {"signals": {name: {"clean": floats(clean), "noisy": floats(noisy)} for name, (clean, noisy) in signals.items()},
            "cases": cases}


def spectral_json():
    fs = 8000.0
    x = np.array(signal(5000))
    cases = []
    for nperseg, noverlap in [(256, 128), (1024, 512), (1024, 0)]:
        f, p = scipy.signal.welch(x, fs, window="hann", nperseg=nperseg, noverlap=noverlap)
        cases.append({"nperseg": nperseg, "noverlap": noverlap, "f": floats(f), "p": floats(p)})
    stft = []
    for n in (64, 256):
        _, _, z = scipy.signal.stft(x, fs, window="hann", nperseg=n, noverlap=3 * n // 4, boundary=None, padded=False)
        stft.append({"n": n, "hop": n // 4, "power": [floats(np.abs(z[:, k]) ** 2) for k in range(z.shape[1])]})
    _, pw = scipy.signal.welch(x, fs, window="hann", nperseg=1024, noverlap=512)
    db = 10 * np.log10(pw / pw.max() + 1e-30)
    peaks = []
    for prominence, distance in [(12, 8), (3, 1), (20, 30)]:
        idx, props = scipy.signal.find_peaks(db, prominence=prominence, distance=distance)
        peaks.append({"prominence": prominence, "distance": distance, "index": [int(i) for i in idx],
                      "prominences": floats(props["prominences"])})
    plateau = [0, 1, 1, 0, 2, 2, 2, 1, 3, 3, 0, 5, 5]
    flat = scipy.signal.find_peaks(plateau)[0]
    return {"fs": fs, "signal": floats(x), "cases": cases, "stft": stft, "welch_db": floats(db), "peaks": peaks,
            "plateau": plateau, "plateau_peaks": [int(i) for i in flat]}


def tables_inc():
    lines = [f"// Generated by tools/gen_golden.py from PyWavelets {pywt.__version__}; regenerate, do not edit."]
    for name in TABLE_NAMES:
        w = pywt.Wavelet(name)
        dec = ", ".join(float(v).hex() for v in w.dec_lo)
        rec = ", ".join(float(v).hex() for v in w.rec_lo)
        lines.append(f'{{"{name}", {{{dec}}}, {{{rec}}}}},')
    return "\n".join(lines) + "\n"


def processing_json(sample_wav):
    """Inputs and numbers of the canvas «Обработка» (docs/design/gen_screens.py), with the noisy fragment stored."""
    _, data = scipy.io.wavfile.read(sample_wav)
    left = data[:, 0].astype(float) / 32768
    n, levels, wave = 8192, 5, "sym8"
    clean = left[100_000:100_000 + n]
    sigma_noise = np.sqrt(np.mean(clean ** 2) / 10)
    noisy = clean + np.random.default_rng(7).normal(0, sigma_noise, n)
    coeffs = pywt.wavedec(noisy, wave, level=levels, mode="symmetric")
    d1 = coeffs[-1]
    sigma = float(np.median(np.abs(d1[d1 != 0])) / 0.6744897501960817)
    bayes = [float(sigma ** 2 / np.sqrt(max(np.mean(c ** 2) - sigma ** 2, np.finfo(float).eps))) for c in coeffs[1:]]
    visu = float(sigma * np.sqrt(2 * np.log(n)))

    def snr(ref, est):
        return float(10 * np.log10(np.sum(ref ** 2) / np.sum((ref - est) ** 2)))

    combos = {}
    for rule in ["bayes", "visu"]:
        for mode in ["soft", "hard"]:
            for scale in [0.5, 1.0, 2.0]:
                new, kept = [coeffs[0]], coeffs[0].size
                for k, c in enumerate(coeffs[1:]):
                    lam = scale * (bayes[k] if rule == "bayes" else visu)
                    new.append(pywt.threshold(c, lam, mode))
                    kept += int(np.count_nonzero(np.abs(c) >= lam))
                y = pywt.waverec(new, wave, mode="symmetric")[:n]
                combos[f"{rule}/{mode}/{scale}"] = {"snr_out": snr(clean, y), "kept": kept,
                                                     "err_db": float(10 * np.log10(np.sum((noisy - y) ** 2) / np.sum(noisy ** 2)))}
    flat = np.concatenate(coeffs)
    order = np.argsort(-np.abs(flat), kind="stable")
    curve = []
    for k in [1, 10, 100, 1000, int(flat.size)]:
        keep = np.zeros_like(flat)
        keep[order[:k]] = flat[order[:k]]
        parts, at = [], 0
        for c in coeffs:
            parts.append(keep[at:at + c.size])
            at += c.size
        y = pywt.waverec(parts, wave, mode="symmetric")[:n]
        curve.append([k, float(10 * np.log10(np.sum((noisy - y) ** 2) / np.sum(noisy ** 2)))])
    np_, period = 1024, 2 ** levels
    bins = [4, 16, 64, 200, 400, 511]
    transfer = {}
    for key, gains in [("flat", [1, 1, 1, 1, 1, 1]), ("bass", [2, 2, 1.4, 1, 1, 1]), ("notch_d3", [1, 1, 1, 0, 1, 1])]:
        t_db, a_db = [], []
        for m in bins:
            k = np.arange(np_)
            ys = []
            for part in (np.cos(2 * np.pi * m * k / np_), np.sin(2 * np.pi * m * k / np_)):
                c = pywt.wavedec(part, wave, level=levels, mode="periodization")
                ys.append(pywt.waverec([g * b for g, b in zip(gains, c)], wave, mode="periodization"))
            spectrum = np.fft.fft(ys[0] + 1j * ys[1]) / np_
            others = [(m + j * np_ // period) % np_ for j in range(1, period)]
            t_db.append(float(20 * np.log10(abs(spectrum[m]) + 1e-12)))
            a_db.append(float(10 * np.log10(np.sum(np.abs(spectrum[others]) ** 2) + 1e-24)))
        transfer[key] = {"gains": gains, "t_db": t_db, "a_db": a_db}
    return {"n": n, "levels": levels, "wavelet": wave, "clean": floats(clean), "noisy": floats(noisy), "sigma": sigma,
            "bayes": bayes, "visu": visu, "snr_in": snr(clean, noisy), "combos": combos, "curve": curve,
            "bins": bins, "np": np_, "transfer": transfer}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--tables", required=True, type=Path)
    parser.add_argument("--fixtures", type=Path)
    args = parser.parse_args()
    fixtures = args.fixtures or args.out.parent / "fixtures" / "wav"
    sample_wav = Path(__file__).resolve().parent.parent / "resources" / "samples" / "test.wav"
    warnings.simplefilter("ignore")
    args.out.mkdir(parents=True, exist_ok=True)
    for file_name, builder in [("filters.json", filters_json), ("pad.json", pad_json),
                               ("dwt.json", dwt_json), ("wavedec.json", wavedec_json),
                               ("wav.json", lambda: wav_json(fixtures)), ("signals.json", signals_json),
                               ("kaiser.json", kaiser_json), ("upfirdn.json", upfirdn_json),
                               ("freqz.json", freqz_json), ("threshold.json", threshold_json),
                               ("denoise.json", lambda: denoise_json(sample_wav)),
                               ("spectral.json", spectral_json), ("swt.json", swt_json),
                               ("packets.json", packets_json), ("cwt.json", cwt_json),
                               ("processing.json", lambda: processing_json(sample_wav))]:
        text = json.dumps(builder(), separators=(",", ":"), allow_nan=False)
        (args.out / file_name).write_text(text + "\n")
    args.tables.parent.mkdir(parents=True, exist_ok=True)
    args.tables.write_text(tables_inc())


if __name__ == "__main__":
    main()
