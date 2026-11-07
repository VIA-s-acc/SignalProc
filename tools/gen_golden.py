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

def complex_list(a):
    a = np.sort_complex(np.atleast_1d(np.asarray(a, dtype=complex)))
    return [[float(v.real), float(v.imag)] for v in a]


def iir_json():
    import scipy.special
    special = {"ellipk": [], "ellipj": []}
    for m in [0.0, 1e-12, 0.1, 0.5, 0.9, 0.99, 1 - 1e-9]:
        special["ellipk"].append({"m": m, "k": float(scipy.special.ellipk(m)), "km1": float(scipy.special.ellipkm1(1 - m)) if m < 1 else None})
    for u in [0.0, 0.3, 1.1, 2.5]:
        for m in [0.0, 0.2, 0.7, 0.999]:
            sn, cn, dn, ph = scipy.special.ellipj(u, m)
            special["ellipj"].append({"u": u, "m": m, "sn": float(sn), "cn": float(cn), "dn": float(dn)})
    prototypes = []
    for n in range(1, 11):
        for name, args in [("butter", ()), ("cheby1", (1.0,)), ("cheby2", (40.0,)), ("ellip", (1.0, 40.0)), ("bessel", ())]:
            fn = {"butter": scipy.signal.buttap, "cheby1": scipy.signal.cheb1ap, "cheby2": scipy.signal.cheb2ap,
                  "ellip": scipy.signal.ellipap, "bessel": scipy.signal.besselap}[name]
            z, pz, k = fn(n, *args)
            prototypes.append({"family": name, "n": n, "z": complex_list(z), "p": complex_list(pz), "k": float(k)})
    digital = []
    w = np.linspace(0, np.pi, 512, endpoint=False)
    for ftype, rp, rs in [("butter", None, None), ("cheby1", 1.0, None), ("cheby2", None, 40.0), ("ellip", 0.5, 60.0), ("bessel", None, None)]:
        for btype, wn in [("lowpass", 0.3), ("highpass", 0.6), ("bandpass", [0.2, 0.45]), ("bandstop", [0.3, 0.5])]:
            for n in [1, 4, 9]:
                z, pz, k = scipy.signal.iirfilter(n, wn, rp=rp, rs=rs, btype=btype, ftype=ftype, output="zpk")
                sos = scipy.signal.zpk2sos(z, pz, k)
                _, h = scipy.signal.sosfreqz(sos, worN=w)
                digital.append({"family": ftype, "type": btype, "n": n, "wn": wn if isinstance(wn, list) else [wn],
                                "rp": rp, "rs": rs, "z": complex_list(z), "p": complex_list(pz), "k": float(k),
                                "mag": floats(np.abs(h))})
    return {"special": special, "prototypes": prototypes, "digital": digital}

def fir_json():
    remez = []
    for numtaps, bands, desired, weight, kind in [
        (31, [0, 0.2, 0.3, 0.5], [1, 0], None, "bandpass"),
        (32, [0, 0.2, 0.3, 0.5], [1, 0], [1, 10], "bandpass"),
        (45, [0, 0.1, 0.15, 0.3, 0.35, 0.5], [0, 1, 0], [10, 1, 10], "bandpass"),
        (31, [0.05, 0.45], [1], None, "hilbert"),
        (30, [0.05, 0.45], [1], None, "hilbert"),
        (21, [0, 0.4], [1], None, "differentiator"),
        (31, [0, 0.37], [1], None, "differentiator"),
        (30, [0.02, 0.41], [1], None, "differentiator"),
    ]:
        h = scipy.signal.remez(numtaps, bands, desired, weight=weight, type=kind, fs=1.0)
        remez.append({"numtaps": numtaps, "bands": bands, "desired": desired, "weight": weight, "type": kind, "h": floats(h)})
    firls = []
    for numtaps, bands, desired, weight in [
        (31, [0, 0.3, 0.4, 1], [1, 1, 0, 0], None),
        (41, [0, 0.2, 0.25, 0.5, 0.55, 1], [0, 0, 1, 1, 0, 0], [10, 1, 10]),
        (21, [0, 1], [0, 1], None),
    ]:
        h = scipy.signal.firls(numtaps, bands, desired, weight=weight)
        firls.append({"numtaps": numtaps, "bands": bands, "desired": desired, "weight": weight, "h": floats(h)})
    firwin = []
    for numtaps, cutoff, window, pass_zero in [
        (31, [0.25], "hamming", True), (31, [0.25], "hann", True), (31, [0.25], "boxcar", True), (31, [0.25], "blackman", True),
        (41, [0.3], ("kaiser", 5.0), True), (41, [0.5], "hamming", False), (51, [0.2, 0.4], "blackman", False),
        (51, [0.3, 0.6], "hamming", True), (30, [0.4], "hann", True), (61, [0.1, 0.3, 0.5, 0.7], "hamming", True),
        (33, [0.45], ("kaiser", 8.6), False), (21, [0.1, 0.9], "boxcar", False)]:
        h = scipy.signal.firwin(numtaps, cutoff, window=window, pass_zero=pass_zero, fs=2.0)
        name, beta = (window, 0.0) if isinstance(window, str) else window
        firwin.append({"numtaps": numtaps, "cutoff": cutoff, "window": name, "beta": beta, "pass_zero": pass_zero, "h": floats(h)})
    firwin2 = []
    for numtaps, freq, gain, window in [
        (33, [0, 0.4, 0.5, 1], [1, 1, 0, 0], "hamming"), (51, [0, 0.2, 0.25, 0.6, 0.65, 1], [0, 0, 1, 1, 0, 0], "hann"),
        (31, [0, 1], [0, 1], "boxcar"), (64, [0, 0.3, 1], [1, 0.5, 0], "blackman")]:
        h = scipy.signal.firwin2(numtaps, freq, gain, window=window, fs=2.0)
        firwin2.append({"numtaps": numtaps, "freq": freq, "gain": gain, "window": window, "h": floats(h)})
    return {"remez": remez, "firls": firls, "firwin": firwin, "firwin2": firwin2}

def ops_json():
    x = np.concatenate([np.linspace(-1.5, 1.5, 61), [0.3333333333333333, -0.7071067811865476, 1e-9, -1e-9]])
    mu = 255.0
    clipped = np.clip(x, -1.0, 1.0)
    mulaw = np.sign(clipped) * np.log1p(mu * np.abs(clipped)) / np.log1p(mu)
    rng = np.random.default_rng(5)
    y = rng.uniform(-1.2, 1.2, 200)
    quant = []
    for bits in [1, 2, 8, 16, 24]:
        step = 2.0 ** (1 - bits)
        quant.append({"bits": bits, "y": floats(np.clip(np.round(y / step) * step, -1.0, 1.0 - step))})
    from skimage.restoration import cycle_spin
    doppler = pywt.data.demo_signal("Doppler", 1024)
    doppler = doppler * 7 / np.std(doppler)
    noisy = doppler + np.random.default_rng(3).normal(0, 1, doppler.size)
    def denoise(v):
        return _denoise._wavelet_threshold(v, "db4", method="BayesShrink", mode="soft", wavelet_levels=5)
    spun = cycle_spin(noisy, func=denoise, max_shifts=7, channel_axis=None)
    import scipy.linalg
    levinson = []
    rng2 = np.random.default_rng(17)
    noise = rng2.standard_normal(400)
    auto = np.correlate(noise, noise, mode="full")[399:399 + 20] / 400
    for r, b in [([4.0, 1.0, 0.5, 0.25], [1.0, 2.0, 3.0, 4.0]), (list(auto), list(rng2.standard_normal(20))),
                 ([2.0, -1.0, 0.0, 0.0, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0, 0.0, 1.0])]:
        levinson.append({"r": floats(np.array(r)), "b": floats(np.array(b)), "x": floats(scipy.linalg.solve_toeplitz(np.array(r), np.array(b)))})
    return {"levinson": levinson, "x": floats(x), "mulaw": floats(mulaw), "y": floats(y), "quantize": quant,
            "cycle_spin": {"noisy": floats(noisy), "shifts": 8, "out": floats(spun)}}

def analysis_json():
    import scipy.fft
    rng = np.random.default_rng(11)
    signals = [rng.standard_normal(37), np.sin(0.3 * np.arange(64)) + 0.1 * rng.standard_normal(64), np.array([0.75])]
    corr = []
    for x, y in [(signals[0], signals[1][:20]), (signals[1], signals[0]), (signals[2], signals[0][:5])]:
        for mode in ["full", "same", "valid"]:
            corr.append({"x": floats(x), "y": floats(y), "mode": mode, "out": floats(scipy.signal.correlate(x, y, mode=mode, method="direct"))})
    hilb, ceps, dct = [], [], []
    for x in signals:
        a = scipy.signal.hilbert(x)
        hilb.append({"x": floats(x), "re": floats(a.real), "im": floats(a.imag)})
        spectrum = np.abs(np.fft.fft(x))
        ceps.append({"x": floats(x), "out": floats(np.real(np.fft.ifft(np.log(spectrum + 1e-12 * spectrum.max()))))})
        dct.append({"x": floats(x), "dct": floats(scipy.fft.dct(x, type=2, norm="ortho"))})
    x = signals[1][:50]
    n, hop = 16, 4
    _, _, z = scipy.signal.stft(x, nperseg=n, noverlap=n - hop, window="hann")
    _, back = scipy.signal.istft(z, nperseg=n, noverlap=n - hop, window="hann")
    stft = {"x": floats(x), "n": n, "hop": hop, "re": [floats(r) for r in z.real.T], "im": [floats(r) for r in z.imag.T], "istft": floats(back)}
    return {"correlate": corr, "hilbert": hilb, "cepstrum": ceps, "dct": dct, "stft": stft}

def blocks_json():
    """Pipeline blocks from M8: numpy/scipy/pywt references on one shared input."""
    fs, n = 8000.0, 200
    t = np.arange(n) / fs
    x = np.sin(2 * np.pi * 440 * t) + 0.5 * np.sin(2 * np.pi * 1800 * t) + 0.3 * np.random.default_rng(23).standard_normal(n)
    windows = []
    for kind, extra in [("hann", None), ("hamming", None), ("blackman", None), ("kaiser", 8.6), ("kaiser", 3.0), ("tukey", 0.5), ("tukey", 0.2)]:
        spec = kind if extra is None else (kind, extra)
        windows.append({"kind": kind, "extra": extra, "w": floats(scipy.signal.get_window(spec, 37, fftbins=False))})
    tc = np.arange(256) / fs
    chirps = [{"method": m, "f0": 100.0, "f1": 3000.0, "y": floats(scipy.signal.chirp(tc, 100.0, tc[-1], 3000.0, method=m))}
              for m in ["linear", "logarithmic"]]
    osc = [{"kind": "square", "duty": 0.3, "width": 1.0, "y": floats(scipy.signal.square(2 * np.pi * 220.0 * tc, 0.3))},
           {"kind": "sawtooth", "duty": 0.5, "width": 1.0, "y": floats(scipy.signal.sawtooth(2 * np.pi * 220.0 * tc, 1.0))},
           {"kind": "sawtooth", "duty": 0.5, "width": 0.25, "y": floats(scipy.signal.sawtooth(2 * np.pi * 220.0 * tc, 0.25))},
           {"kind": "triangle", "duty": 0.5, "width": 1.0, "y": floats(scipy.signal.sawtooth(2 * np.pi * 220.0 * tc, 0.5))}]
    iir = []
    for family, ftype, btype, order, cutoff, rp, rs in [
            ("butterworth", "butter", "lowpass", 4, [1000.0], 1.0, 40.0), ("cheby1", "cheby1", "highpass", 5, [1500.0], 1.0, 40.0),
            ("cheby2", "cheby2", "bandpass", 4, [500.0, 2000.0], 1.0, 40.0), ("ellip", "ellip", "bandstop", 3, [800.0, 1600.0], 0.5, 50.0),
            ("bessel", "bessel", "lowpass", 6, [1200.0], 1.0, 40.0)]:
        sos = scipy.signal.iirfilter(order, cutoff if len(cutoff) > 1 else cutoff[0], rp=rp, rs=rs, btype=btype, ftype=ftype, output="sos", fs=fs)
        iir.append({"family": family, "type": btype, "order": order, "cutoff": cutoff, "rp": rp, "rs": rs,
                    "y": floats(scipy.signal.sosfilt(sos, x)), "zero_phase": floats(scipy.signal.sosfiltfilt(sos, x))})
    fir = []
    for numtaps, cutoff, window, beta, pass_zero in [(31, [1000.0], "hamming", 0.0, True), (33, [500.0, 1500.0], "kaiser", 6.0, False),
                                                     (30, [1500.0], "blackman", 0.0, True)]:
        h = scipy.signal.firwin(numtaps, cutoff, window=window if window != "kaiser" else (window, beta), pass_zero=pass_zero, fs=fs)
        fir.append({"numtaps": numtaps, "cutoff": cutoff, "window": window, "beta": beta, "pass_zero": pass_zero,
                    "y": floats(scipy.signal.lfilter(h, 1.0, x)), "centred": floats(scipy.signal.convolve(x, h, mode="same"))})
    moving = [{"length": 5, "centred": False, "y": floats(scipy.signal.lfilter(np.ones(5) / 5, 1.0, x))},
              {"length": 6, "centred": True, "y": floats(scipy.signal.convolve(x, np.ones(6) / 6, mode="same"))}]
    mra_dwt = pywt.mra(x, "db2", level=3, transform="dwt", mode="symmetric")
    mra_swt = pywt.mra(x[:192], "db2", level=3, transform="swt")
    f, p = scipy.signal.welch(x, fs, window="hann", nperseg=n, noverlap=n // 2)
    k = int(np.argmax(p))
    a, b, c = np.log(p[k - 1]), np.log(p[k]), np.log(p[k + 1])
    rms = float(np.sqrt(np.mean(x ** 2)))
    stats = {"rms": rms, "peak": float(np.max(np.abs(x))), "crest_db": float(20 * np.log10(np.max(np.abs(x)) / rms)),
             "mean": float(np.mean(x)), "std": float(np.std(x)),
             "zcr_per_s": float(np.count_nonzero((x[1:] >= 0) != (x[:-1] >= 0)) * fs / n),
             "centroid_hz": float(np.sum(f * p) / np.sum(p)), "dominant_hz": float((k + 0.5 * (a - c) / (a - 2 * b + c)) * fs / n)}
    return {"fs": fs, "x": floats(x), "windows": windows, "chirp": chirps, "osc": osc, "iir": iir, "fir": fir,
            "median": [{"kernel": k, "y": floats(scipy.signal.medfilt(x, k))} for k in [5, 11]], "moving_average": moving,
            "preemphasis": floats(scipy.signal.lfilter([1.0, -0.97], 1.0, x)), "deemphasis": floats(scipy.signal.lfilter([1.0], [1.0, -0.9], x)),
            "cumsum": floats(np.cumsum(x)), "diff": floats(np.diff(x, prepend=0.0)), "softclip": floats(np.tanh(2.0 * x)),
            "mra_dwt": [floats(v[:n]) for v in mra_dwt], "mra_swt": [floats(v) for v in mra_swt], "stats": stats}

def packet_cost(c, norm2):
    total = 0.0
    for v in c:
        if v != 0 and norm2 > 0:
            q = v * v / norm2
            total -= q * math.log(q)
    return total


def freq_key(level, index, deepest):
    k, s = index, 1
    while s < 64:
        k ^= k >> s
        s <<= 1
    return k << (deepest - level)


def packet_reference(channels, name, mode, levels, basis):
    """Packet leaves of pywt.WaveletPacket in frequency order; the best basis is Coifman-Wickerhauser on the Shannon cost summed over channels."""
    trees = []
    for x in channels:
        wp = pywt.WaveletPacket(np.asarray(x), name, mode=mode, maxlevel=levels)
        nodes = {(0, 0): list(map(float, x))}
        for j in range(1, levels + 1):
            for node in wp.get_level(j, "natural"):
                nodes[(j, int(node.path.replace("a", "0").replace("d", "1"), 2))] = floats(node.data)
        trees.append(nodes)
    if basis == "full":
        leaves = [(levels, k) for k in range(2 ** levels)]
    elif basis == "dwt":
        leaves = [(levels, 0)] + [(j, 1) for j in range(levels, 0, -1)]
    else:
        norms = [sum(v * v for v in t[(0, 0)]) for t in trees]
        best, split = {}, {}
        for j in range(levels, -1, -1):
            for k in range(2 ** j):
                own = 0.0
                for t, norm2 in zip(trees, norms):
                    own += packet_cost(t[(j, k)], norm2)
                best[(j, k)], split[(j, k)] = own, False
                if j < levels and best[(j + 1, 2 * k)] + best[(j + 1, 2 * k + 1)] < own:
                    best[(j, k)], split[(j, k)] = best[(j + 1, 2 * k)] + best[(j + 1, 2 * k + 1)], True
        leaves, stack = [], [(0, 0)]
        while stack:
            j, k = stack.pop()
            if split[(j, k)]:
                stack += [(j + 1, 2 * k + 1), (j + 1, 2 * k)]
            else:
                leaves.append((j, k))
    deepest = max(j for j, _ in leaves)
    leaves.sort(key=lambda n: freq_key(n[0], n[1], deepest))
    paths = ["".join("d" if (k >> (j - 1 - i)) & 1 else "a" for i in range(j)) for j, k in leaves]
    return {"wavelet": name, "mode": mode, "levels": levels, "basis": basis, "paths": paths,
            "bands": [[t[n] for n in leaves] for t in trees]}


def levinson_durbin(r, order):
    a, err, ks = np.zeros(order + 1), r[0], []
    a[0] = 1.0
    for m in range(1, order + 1):
        acc = r[m] + sum(a[j] * r[m - j] for j in range(1, m))
        k = -acc / err
        prev = a.copy()
        for j in range(1, m):
            a[j] = prev[j] + k * prev[m - j]
        a[m] = k
        err *= 1 - k * k
        ks.append(k)
    return ks


def smoothing(ms, fs):
    samples = ms / 1000 * fs
    return math.exp(-1 / samples) if samples > 0 else 0.0


def level_db(channels, detector, rms_coef=0.0):
    frames = np.zeros((len(channels), max(len(c) for c in channels)))
    for i, c in enumerate(channels):
        frames[i, :len(c)] = c
    if detector == "peak":
        return [20 * math.log10(max(v, 1e-10)) for v in np.max(np.abs(frames), axis=0)]
    out, power = [], 0.0
    for n in range(frames.shape[1]):
        square = 0.0
        for c in frames[:, n]:
            square += c * c
        power = rms_coef * power + (1 - rms_coef) * (square / len(channels))
        out.append(10 * math.log10(max(power, 1e-20)))
    return out


def dynamics(channels, level, target_of, attack, release, opening_uses_attack=False, makeup=0.0):
    state, gains = 0.0, []
    for v in level:
        target = target_of(v)
        rising = target > state if opening_uses_attack else target < state
        coef = attack if rising else release
        state = coef * state + (1 - coef) * target
        gains.append(state + makeup)
    g = np.array(gains)
    return [floats(np.asarray(c) * 10.0 ** (g[:len(c)] / 20)) for c in channels]


def compressor_curve(threshold, slope, knee):
    def target(x):
        over = x - threshold
        if knee > 0 and 2 * abs(over) <= knee:
            return x + (slope - 1) * (over + knee / 2) * (over + knee / 2) / (2 * knee) - x
        if over > 0:
            return threshold + over * slope - x
        return 0.0
    return target


def welch_db(x, fs, segment):
    n = min(len(x), segment)
    _, p = scipy.signal.welch(x, fs, window="hann", nperseg=n, noverlap=n // 2)
    return n, p, 10 * np.log10(np.maximum(p, 1e-20))


OCTAVES = ["31.5", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
THIRDS = ["25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500", "630", "800", "1k",
          "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k"]


def band_power_reference(x, fs, bands, segment):
    names, step, first = (THIRDS, 1.0, -16.0) if bands == "third" else (OCTAVES, 3.0, -5.0)
    edges = [1000 * 10.0 ** ((step * (first + i) - step / 2) / 10) for i in range(len(names) + 1)]
    count = 0
    while count < len(names) and edges[count] < fs / 2:
        count += 1
    n, p, _ = welch_db(x, fs, segment)
    power, total = [0.0] * (len(names) + 2), 0.0
    for k, v in enumerate(p):
        slot = int(np.searchsorted(edges, k * (fs / n), side="right"))
        power[slot] += v
        total += v
    fields = {"below_22hz": power[0] / total}
    for i in range(count):
        fields[names[i] + "hz"] = power[i + 1] / total
    fields["above_22khz"] = power[-1] / total
    return {"bands": bands, "segment": segment, "fields": fields}


def blocks2_json():
    """Second batch of pipeline blocks (M8c): numpy/scipy/pywt references on one shared input."""
    fs, n = 8000.0, 200
    t = np.arange(n) / fs
    x = np.sin(2 * np.pi * 440 * t) + 0.5 * np.sin(2 * np.pi * 1800 * t) + 0.3 * np.random.default_rng(23).standard_normal(n)
    x2 = 0.8 * np.cos(2 * np.pi * 300 * t) + 0.4 * np.random.default_rng(29).standard_normal(n)
    # The dwt block keeps the detail sign of the project's banks, which for db2 is the opposite of pywt's.
    dwt = [floats(b) if i == 0 else floats(-b) for i, b in enumerate(pywt.wavedec(x, "db2", level=3, mode="symmetric"))]

    keep = []
    for mode, amount, keep_approx in [("percent", 10.0, True), ("count", 25.0, False), ("percent", 37.5, True)]:
        bands = [np.array(b) for b in dwt]
        first = 1 if keep_approx else 0
        pool = np.concatenate(bands[first:])
        k = min(len(pool), int(math.floor((amount / 100 * len(pool) if mode == "percent" else amount) + 0.5)))
        mask = np.zeros(len(pool), dtype=bool)
        mask[np.argsort(-np.abs(pool), kind="stable")[:k]] = True
        at = 0
        for b in range(first, len(bands)):
            bands[b] = np.where(mask[at:at + len(bands[b])], bands[b], 0.0)
            at += len(bands[b])
        keep.append({"mode": mode, "amount": amount, "keep_approx": keep_approx, "bands": [floats(b) for b in bands]})

    quantize = []
    for mode, bits, step in [("bits", 4, 0.0), ("bits", 9, 0.0), ("step", 0, 0.05)]:
        bands = [np.array(b) for b in dwt]
        for b in range(1, len(bands)):
            d = np.max(np.abs(bands[b])) / (2.0 ** (bits - 1) - 1) if mode == "bits" else step
            bands[b] = np.sign(bands[b]) * np.floor(np.abs(bands[b]) / d + 0.5) * d
        quantize.append({"mode": mode, "bits": bits, "step": step, "bands": [floats(b) for b in bands]})

    packets = [packet_reference([x], "db2", "symmetric", 3, "best"), packet_reference([x], "sym4", "periodization", 3, "full"),
               packet_reference([x], "haar", "periodization", 2, "dwt"), packet_reference([x], "coif1", "zero", 4, "best"),
               packet_reference([x, x2], "db2", "periodization", 4, "best")]

    import scipy.linalg
    lpc = []
    for order in [4, 12]:
        r = np.array([float(np.dot(x[k:], x[:n - k])) for k in range(order + 1)])
        a = scipy.linalg.solve_toeplitz(r[:order], -r[1:order + 1])
        err = r[0] + float(np.dot(a, r[1:order + 1]))
        lpc.append({"order": order, "a": floats(a), "k": floats(levinson_durbin(r, order)), "gain_db": float(10 * np.log10(r[0] / err)),
                    "residual": floats(scipy.signal.lfilter(np.r_[1.0, a], 1.0, x))})

    shift = []
    for samples, mode in [(5, "zero"), (-7, "zero"), (13, "circular"), (-203, "circular")]:
        if mode == "circular":
            y = np.roll(x, samples)
        else:
            y = np.zeros(n)
            if samples >= 0:
                y[samples:] = x[:n - samples]
            else:
                y[:n + samples] = x[-samples:]
        shift.append({"samples": samples, "mode": mode, "y": floats(y)})

    biquad = []
    for ftype, f0, q, gain_db in [("lowpass", 1000.0, 0.7071, 0.0), ("highpass", 600.0, 2.0, 0.0), ("bandpass", 1800.0, 4.0, 0.0),
                                  ("notch", 440.0, 10.0, 0.0), ("peaking", 1200.0, 1.5, 9.0), ("lowshelf", 300.0, 0.7071, -6.0),
                                  ("highshelf", 2500.0, 0.9, 4.5), ("allpass", 900.0, 0.5, 0.0)]:
        A = 10 ** (gain_db / 40)
        w0 = 2 * math.pi * f0 / fs
        cw, alpha = math.cos(w0), math.sin(w0) / (2 * q)
        sq = 2 * math.sqrt(A) * alpha
        a = [1 + alpha, -2 * cw, 1 - alpha]
        b = {"lowpass": [(1 - cw) / 2, 1 - cw, (1 - cw) / 2], "highpass": [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2],
             "bandpass": [alpha, 0.0, -alpha], "notch": [1.0, -2 * cw, 1.0], "allpass": [1 - alpha, -2 * cw, 1 + alpha],
             "peaking": [1 + alpha * A, -2 * cw, 1 - alpha * A],
             "lowshelf": [A * ((A + 1) - (A - 1) * cw + sq), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sq)],
             "highshelf": [A * ((A + 1) + (A - 1) * cw + sq), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sq)]}[ftype]
        if ftype == "peaking":
            a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
        elif ftype == "lowshelf":
            a = [(A + 1) + (A - 1) * cw + sq, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sq]
        elif ftype == "highshelf":
            a = [(A + 1) - (A - 1) * cw + sq, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sq]
        biquad.append({"type": ftype, "f0": f0, "q": q, "gain_db": gain_db, "y": floats(scipy.signal.lfilter(b, a, x))})

    loud = [x * 2.5, x2[:150]]
    dyn = []
    for detector, threshold, ratio, knee, attack, release, makeup, rms_ms in [("peak", -12.0, 4.0, 0.0, 2.0, 30.0, 0.0, 10.0),
                                                                            ("rms", -18.0, 3.0, 6.0, 0.5, 10.0, 4.0, 1.5)]:
        level = level_db(loud, detector, smoothing(rms_ms, fs))
        dyn.append({"block": "compressor", "params": {"detector": detector, "threshold_db": threshold, "ratio": ratio, "knee_db": knee,
                                                      "attack_ms": attack, "release_ms": release, "makeup_db": makeup, "rms_ms": rms_ms},
                    "y": dynamics(loud, level, compressor_curve(threshold, 1 / ratio, knee), smoothing(attack, fs), smoothing(release, fs), makeup=makeup)})
    for attack, release in [(0.0, 20.0), (1.0, 5.0)]:
        level = level_db(loud, "peak")
        dyn.append({"block": "limiter", "params": {"threshold_db": -6.0, "attack_ms": attack, "release_ms": release},
                    "y": dynamics(loud, level, compressor_curve(-6.0, 0.0, 0.0), smoothing(attack, fs), smoothing(release, fs))})
    level = level_db(loud, "peak")
    dyn.append({"block": "gate", "params": {"threshold_db": -6.0, "range_db": 40.0, "attack_ms": 0.5, "release_ms": 3.0},
                "y": dynamics(loud, level, lambda v: -40.0 if v < -6.0 else 0.0, smoothing(0.5, fs), smoothing(3.0, fs), opening_uses_attack=True)})

    delay = int(math.floor(2.0 / 1000 * fs + 0.5))
    wet = scipy.signal.lfilter([0.0] * delay + [1.0], [1.0] + [0.0] * (delay - 1) + [-0.5], x)
    echo = {"delay_ms": 2.0, "feedback": 0.5, "mix": 0.4, "y": floats((1 - 0.4) * x + 0.4 * wet)}
    comb = [{"kind": "feedforward", "delay": 9, "gain": -0.8, "y": floats(scipy.signal.lfilter([1.0] + [0.0] * 8 + [-0.8], [1.0], x))},
            {"kind": "feedback", "delay": 23, "gain": 0.6, "y": floats(scipy.signal.lfilter([1.0], [1.0] + [0.0] * 22 + [-0.6], x))}]

    b = x2[:120]
    ga, gb = 10 ** (-3 / 20), 10 ** (2 / 20)
    u = ga * x
    v = np.zeros(n)
    v[:len(b)] = gb * b
    start, length = math.floor(0.004 * fs + 0.5), math.floor(5.0 / 1000 * fs + 0.5)
    w = np.clip((np.arange(n) - start) / length, 0, 1)
    mix = {"gain_a_db": -3.0, "gain_b_db": 2.0, "at": 0.004, "crossfade_ms": 5.0, "sum": floats(u + v), "crossfade": floats((1 - w) * u + w * v)}

    peaks = []
    for count, segment, prominence in [(3, 4096, 6.0), (5, 64, 3.0)]:
        m, _, db = welch_db(x, fs, segment)
        idx, _ = scipy.signal.find_peaks(db, prominence=prominence)
        order = np.argsort(-db[idx], kind="stable")[:count]
        peaks.append({"count": count, "segment": segment, "prominence_db": prominence,
                      "hz": [float(idx[i] * (fs / m)) for i in order], "db": [float(db[idx[i]]) for i in order]})

    rate = 44100.0
    tl = np.arange(8192) / rate
    tones = (np.sin(2 * np.pi * 100 * tl) + 0.5 * np.sin(2 * np.pi * 1000 * tl) + 0.25 * np.sin(2 * np.pi * 5000 * tl)
             + 0.05 * np.random.default_rng(37).standard_normal(len(tl)))
    bands = [band_power_reference(tones, rate, "octave", 4096), band_power_reference(tones, rate, "third", 4096),
             band_power_reference(x, fs, "octave", 64)]

    return {"fs": fs, "x": floats(x), "x2": floats(x2), "keep_largest": keep, "coeff_quantize": quantize, "wpt": packets, "lpc": lpc,
            "shift": shift, "biquad": biquad, "dynamics": dyn, "echo": echo, "comb": comb, "mix": mix, "spectrum_peaks": peaks,
            "tones_rate": rate, "tones": floats(tones), "band_power": bands}


def adaptive_filter(x, d, taps, mu, rule, eps=1e-8):
    w = np.zeros(taps)
    y = np.zeros(len(x))
    e = np.zeros(len(x))
    padded = np.concatenate([np.zeros(taps - 1), x])
    for n in range(len(x)):
        u = padded[n:n + taps][::-1]
        y[n] = w @ u
        e[n] = d[n] - y[n]
        step = mu * e[n] * u
        w = w + (step / (eps + u @ u) if rule == "nlms" else step)
    return y, e, w


def adaptive_json():
    rng = np.random.default_rng(23)
    x = rng.standard_normal(300)
    h = np.array([0.6, -0.3, 0.2, 0.1, -0.05])
    d = np.convolve(x, h)[:len(x)] + 0.01 * rng.standard_normal(len(x))
    t = np.arange(400)
    tone = np.sin(2 * np.pi * 0.01 * t)
    hum = rng.standard_normal(400)
    noisy = tone + np.convolve(hum, [0.8, 0.4, -0.2])[:400]
    scenarios = []
    for name, xs, ds, taps in [("identify", x, d, 8), ("cancel", hum, noisy, 6)]:
        runs = []
        for rule, step in [("lms", 0.02), ("nlms", 0.5)]:
            y, e, w = adaptive_filter(xs, ds, taps, step, rule)
            runs.append({"rule": rule, "mu": step, "y": floats(y), "e": floats(e), "w": floats(w)})
        scenarios.append({"name": name, "taps": taps, "x": floats(xs), "d": floats(ds), "runs": runs})
    return {"scenarios": scenarios, "system": floats(h)}


def spectral_subtract(x, noise, alpha, beta, n, hop):
    _, _, zn = scipy.signal.stft(noise, nperseg=n, noverlap=n - hop, window="hann")
    level = np.mean(np.abs(zn) ** 2, axis=1)
    _, _, z = scipy.signal.stft(x, nperseg=n, noverlap=n - hop, window="hann")
    power = np.maximum(np.abs(z) ** 2 - alpha * level[:, None], beta * level[:, None])
    _, out = scipy.signal.istft(np.sqrt(power) * np.exp(1j * np.angle(z)), nperseg=n, noverlap=n - hop, window="hann")
    return out[:len(x)]


def restoration_json():
    rng = np.random.default_rng(29)
    t = np.arange(400)
    clean = np.sin(2 * np.pi * 0.033 * t) * np.exp(-((t - 200) / 110.0) ** 2)
    noise = 0.2 * rng.standard_normal(400)
    x = clean + noise
    wiener = []
    for size, level in [(5, None), (11, None), (3, 0.04), (21, 0.01)]:
        wiener.append({"size": size, "noise": level, "out": floats(scipy.signal.wiener(x, size, level))})
    lead = 0.2 * rng.standard_normal(300)
    subtract = []
    for alpha, beta, n, hop in [(2.0, 0.01, 64, 16), (1.0, 0.05, 32, 8), (4.0, 0.001, 128, 64)]:
        subtract.append({"alpha": alpha, "beta": beta, "n": n, "hop": hop, "out": floats(spectral_subtract(x, lead, alpha, beta, n, hop))})
    h = np.array([1.0, 0.5, -0.25, 0.125])
    blurred = np.real(np.fft.ifft(np.fft.fft(clean) * np.fft.fft(h, len(clean))))
    deconv = []
    for snr in [20.0, 40.0, 80.0]:
        Y = np.fft.fft(blurred)
        H = np.fft.fft(h, len(blurred))
        X = Y * np.conj(H) / (np.abs(H) ** 2 + 10 ** (-snr / 10))
        deconv.append({"snr_db": snr, "out": floats(np.real(np.fft.ifft(X)))})
    return {"x": floats(x), "clean": floats(clean), "noise": floats(lead), "blurred": floats(blurred), "h": floats(h),
            "wiener": wiener, "subtract": subtract, "deconvolve": deconv}


def toeplitz_json():
    import scipy.linalg
    rng = np.random.default_rng(31)
    h0 = np.array(pywt.Wavelet("db4").dec_lo)
    auto = np.correlate(h0, h0, mode="full")[len(h0) - 1:]
    cases = []
    for r in [np.concatenate([auto, np.zeros(4)]), np.array([4.0, 1.0, 0.5, 0.25, 0.125]), None]:
        if r is None:
            a = rng.standard_normal(12)
            r = np.correlate(a, a, mode="full")[len(a) - 1:][:9] + np.eye(1, 9)[0] * 0.5
        inv = np.linalg.inv(scipy.linalg.toeplitz(r))
        cases.append({"r": floats(r), "inverse": [floats(row) for row in inv]})
    return {"cases": cases}


FREEVERB_COMBS = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617]
FREEVERB_ALLPASSES = [556, 441, 341, 225]


def fx_phase(n, rate, fs):
    return 2 * np.pi * rate * np.arange(n) / fs


def tremolo(x, fs, rate, depth, shape):
    ph = fx_phase(len(x), rate, fs)
    lfo = {"sine": np.sin(ph), "triangle": scipy.signal.sawtooth(ph, 0.5), "square": scipy.signal.square(ph)}[shape]
    return x * (1 - depth * (1 - lfo) / 2)


def modulated_delay(x, fs, mode, rate, depth_ms, base_ms, feedback, mix):
    fb = feedback if mode == "flanger" else 0.0
    m = 1.0 if mode == "vibrato" else mix
    buf, y = np.zeros(len(x)), np.zeros(len(x))
    for k in range(len(x)):
        d = max(1.0, (base_ms + depth_ms * (1 + math.sin(2 * math.pi * rate * k / fs)) / 2) * fs / 1000)
        pos = k - d
        i0 = math.floor(pos)
        frac = pos - i0
        a = buf[i0] if i0 >= 0 else 0.0
        b = buf[i0 + 1] if frac > 0 and i0 + 1 >= 0 else 0.0
        wet = (1 - frac) * a + frac * b
        buf[k] = x[k] + fb * wet
        y[k] = (1 - m) * x[k] + m * wet
    return y


def phaser(x, fs, stages, rate, min_hz, max_hz, feedback, mix):
    xs, ys = np.zeros(stages), np.zeros(stages)
    last, y = 0.0, np.zeros(len(x))
    for k in range(len(x)):
        fc = min_hz * (max_hz / min_hz) ** ((1 - math.cos(2 * math.pi * rate * k / fs)) / 2)
        t = math.tan(math.pi * fc / fs)
        a = (t - 1) / (t + 1)
        u = x[k] + feedback * last
        for s in range(stages):
            v = a * (u - ys[s]) + xs[s]
            xs[s], ys[s], u = u, v, v
        last = u
        y[k] = (1 - mix) * x[k] + mix * u
    return y


def reverb(x, fs, room_size, damping, wet, dry):
    scale = lambda length: max(1, math.floor(length * fs / 44100 + 0.5))
    fb, damp = 0.28 * room_size + 0.7, 0.4 * damping
    acc = np.zeros(len(x))
    for length in map(scale, FREEVERB_COMBS):
        buf, idx, store = np.zeros(length), 0, 0.0
        for k in range(len(x)):
            out = buf[idx]
            store = out * (1 - damp) + store * damp
            buf[idx] = 0.015 * x[k] + store * fb
            idx = (idx + 1) % length
            acc[k] += out
    for length in map(scale, FREEVERB_ALLPASSES):
        buf, idx = np.zeros(length), 0
        for k in range(len(x)):
            held = buf[idx]
            buf[idx] = acc[k] + held * 0.5
            acc[k] = held - acc[k]
            idx = (idx + 1) % length
    return dry * x + wet * 3 * acc


def shifted(c, d):
    y = np.zeros(len(c))
    for k in range(len(c)):
        if 0 <= k - d < len(c):
            y[k] = c[k - d]
    return y


def blocks_fx_json():
    """Effect, modulation, math and channel blocks: numpy/scipy references and the exact equations where scipy has none."""
    fs, n = 8000.0, 400
    t = np.arange(n) / fs
    rng = np.random.default_rng(37)
    x = np.sin(2 * np.pi * 300 * t) + 0.4 * np.sin(2 * np.pi * 1700 * t) + 0.2 * rng.standard_normal(n)
    r = 0.7 * np.cos(2 * np.pi * 500 * t) + 0.2 * rng.standard_normal(n)
    positive = np.abs(x) + 0.1
    trem = [{"shape": s, "rate": 7.0, "depth": 0.8, "y": floats(tremolo(x, fs, 7.0, 0.8, s))} for s in ["sine", "triangle", "square"]]
    mdelay = [{"mode": m, "rate": rate, "depth_ms": dm, "base_ms": bm, "feedback": fb, "mix": mix,
               "y": floats(modulated_delay(x, fs, m, rate, dm, bm, fb, mix))}
              for m, rate, dm, bm, fb, mix in [("chorus", 1.5, 4.0, 10.0, 0.6, 0.5), ("flanger", 3.0, 2.0, 0.5, 0.7, 0.5),
                                               ("vibrato", 6.0, 3.0, 1.0, 0.0, 0.3), ("flanger", 2.0, 1.0, 0.0, -0.8, 0.7)]]
    phasers = [{"stages": s, "rate": rate, "min_hz": lo, "max_hz": hi, "feedback": fb, "mix": mix,
                "y": floats(phaser(x, fs, s, rate, lo, hi, fb, mix))}
               for s, rate, lo, hi, fb, mix in [(4, 2.0, 200.0, 2000.0, 0.5, 0.5), (12, 5.0, 3000.0, 100.0, -0.7, 0.8)]]
    reverbs = [{"room_size": rs, "damping": dp, "wet": w, "dry": d, "y": floats(reverb(x, fs, rs, dp, w, d))}
               for rs, dp, w, d in [(0.5, 0.5, 0.33, 0.7), (0.95, 0.1, 1.0, 0.0)]]
    u = 3.0 * x
    v = np.mod(u + 1, 4)
    folds = {"triangle": floats(np.where(v < 2, v - 1, 3 - v)), "sine": floats(np.sin(np.pi / 2 * u))}
    ph = fx_phase(n, 1200.0, fs)
    integral = np.concatenate([[0.0], np.cumsum(x)[:-1]])
    gp_t = (np.arange(321) - 320 / 2) / fs
    pulses = [{"fc": fc, "bw": bw, "bwr": bwr, "y": floats(0.8 * scipy.signal.gausspulse(gp_t, fc=fc, bw=bw, bwr=bwr))}
              for fc, bw, bwr in [(1000.0, 0.5, -6.0), (2500.0, 0.2, -20.0)]]
    train = np.zeros(int(0.05 * fs))
    train[7::33] = 0.5
    math_ops = {"log": np.log(positive), "log10": np.log10(positive), "exp": np.exp(x), "sqrt": np.sqrt(positive),
                "pow": positive ** 2.5, "sign": np.sign(np.round(x, 1)), "reciprocal": 1 / positive}
    short = r[:300]
    padded = np.concatenate([short, np.zeros(n - len(short))])
    exponent = 0.5 + np.abs(np.sin(2 * np.pi * 90 * t))
    binary = {"divide": floats(x / (np.abs(r) + 0.05)), "min": floats(np.minimum(x, padded)), "max": floats(np.maximum(x, padded)),
              "hypot": floats(np.hypot(x, padded)), "pow": floats(positive ** exponent)}
    mid, side = (x + r) / 2, (x - r) / 2
    hold = x[np.arange(n) // 5 * 5]
    return {"fs": fs, "x": floats(x), "r": floats(r), "positive": floats(positive), "exponent": floats(exponent),
            "tremolo": trem, "modulated_delay": mdelay, "phaser": phasers, "reverb": reverbs, "wavefold": folds,
            "am": floats((1 + 0.6 * x) * np.cos(ph)), "fm": floats(np.cos(ph + 2 * np.pi * 150.0 * integral / fs)),
            "gausspulse": pulses, "impulse_train": floats(train), "affine": floats(-1.5 * x + 0.25),
            "math": {k: floats(v) for k, v in math_ops.items()}, "sign_input": floats(np.round(x, 1)), "binary": binary,
            "repeat": floats(np.tile(x[:50], 3)), "channel_delay": [floats(shifted(x, 0)), floats(shifted(r, 12)), floats(shifted(x, -5))],
            "stereo_width": [floats(mid + 0.4 * side), floats(mid - 0.4 * side)],
            "balance": [floats(np.sqrt(1 - 0.6) * x), floats(np.sqrt(1 + 0.6) * r)], "sample_hold": floats(hold),
            "dc_block": floats(scipy.signal.lfilter([1.0, -1.0], [1.0, -0.99], x + 0.5))}

def yin(x, fs, fmin, fmax, frame, hop, threshold):
    w = frame // 2
    tau_min, tau_max = max(1, int(math.floor(fs / fmax))), int(math.ceil(fs / fmin))
    f0 = []
    for start in range(0, len(x) - frame + 1, hop):
        seg = x[start:start + frame]
        d = np.zeros(tau_max + 1)
        for tau in range(1, tau_max + 1):
            diff = seg[:w] - seg[tau:tau + w]
            d[tau] = np.dot(diff, diff)
        cmnd, acc = np.ones(tau_max + 1), 0.0
        for tau in range(1, tau_max + 1):
            acc += d[tau]
            cmnd[tau] = d[tau] * tau / acc if acc > 0 else 1.0
        tau = next((t for t in range(tau_min, tau_max + 1) if cmnd[t] < threshold), None)
        if tau is None:
            f0.append(0.0)
            continue
        while tau + 1 <= tau_max and cmnd[tau + 1] < cmnd[tau]:
            tau += 1
        delta = 0.0
        if tau - 1 >= 1 and tau + 1 <= tau_max:
            a, b, c = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
            if a - 2 * b + c > 0:
                delta = 0.5 * (a - c) / (a - 2 * b + c)
        f0.append(fs / (tau + delta))
    return np.array(f0)


def stft_power(x, n, hop):
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)
    scale = 1.0 / np.sum(window) ** 2
    return np.array([np.abs(np.fft.rfft(x[s:s + n] * window)) ** 2 * scale for s in range(0, len(x) - n + 1, hop)])


def mel_bank(fs, n, n_mels):
    freqs = np.arange(n // 2 + 1) * fs / n
    edges = 700 * (10 ** (np.linspace(0, 2595 * np.log10(1 + fs / 2 / 700), n_mels + 2) / 2595) - 1)
    bank = np.zeros((n_mels, len(freqs)))
    for m in range(n_mels):
        lo, mid, hi = edges[m], edges[m + 1], edges[m + 2]
        bank[m] = np.maximum(0, np.minimum((freqs - lo) / (mid - lo), (hi - freqs) / (hi - mid)))
    return bank


def spectral_features(x, fs, n, hop, pct):
    p = stft_power(x, n, hop)
    f = np.arange(n // 2 + 1) * fs / n
    m = np.sqrt(p)
    centroid, spread, rolloff, flatness = [], [], [], []
    for frame_p, frame_m in zip(p, m):
        total = frame_m.sum()
        if not total > 0:
            continue
        c = np.sum(f * frame_m) / total
        centroid.append(c)
        spread.append(np.sqrt(np.sum((f - c) ** 2 * frame_m) / total))
        rolloff.append(f[np.argmax(np.cumsum(frame_m) >= pct / 100 * total)])
        floored = np.maximum(frame_p, 1e-10)
        flatness.append(np.exp(np.mean(np.log(floored))) / np.mean(floored))
    flux = [np.sqrt(np.sum((m[t] - m[t - 1]) ** 2)) for t in range(1, len(m))]
    mean = lambda v: float(np.mean(v)) if len(v) else 0.0
    return {"centroid_hz": mean(centroid), "spread_hz": mean(spread), "rolloff_hz": mean(rolloff), "flatness": mean(flatness),
            "flux": mean(flux), "frames": float(len(m))}


def delay_report(a, b, fs):
    r = scipy.signal.correlate(b, a, mode="full", method="direct")
    k = int(np.argmax(np.abs(r)))
    delta = 0.0
    if 0 < k < len(r) - 1:
        y0, y1, y2 = abs(r[k - 1]), abs(r[k]), abs(r[k + 1])
        if y0 - 2 * y1 + y2 < 0:
            delta = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    lag = k - (len(a) - 1) + delta
    return {"lag_samples": lag, "lag_s": lag / fs, "peak_lag": float(k - (len(a) - 1)),
            "peak_corr": float(r[k] / np.sqrt(np.dot(a, a) * np.dot(b, b)))}


def blocks_analysis_json():
    """M8d analysis and design blocks: numpy/scipy/pywt references; conventions are stated in the C++ block sources."""
    import scipy.fft
    import scipy.stats
    fs, n = 8000.0, 300
    t = np.arange(n) / fs
    rng = np.random.default_rng(29)
    x = np.sin(2 * np.pi * 440 * t) + 0.5 * np.sin(2 * np.pi * 1800 * t) + 0.3 * rng.standard_normal(n)
    lfilter = []
    for b, a in [([0.2, 0.3, 0.1], [1.0, -0.5, 0.25]), ([1.0, 2.0, 1.0], [2.0, -0.6, 0.2]), ([0.5, -0.25], [1.0]), ([1.0], [1.0, -1.0])]:
        lfilter.append({"b": b, "a": a, "y": floats(scipy.signal.lfilter(b, a, x))})
    taps = [0.1, -0.2, 0.5, 0.25, 0.05, 0.3]
    fir_taps = {"taps": taps, "y": floats(np.convolve(x, taps)[:n]), "centred": floats(np.convolve(x, taps)[(len(taps) - 1) // 2:][:n])}
    savgol = []
    for window, order, deriv, mode in [(11, 3, 0, "interp"), (21, 4, 1, "interp"), (9, 2, 0, "mirror"), (7, 3, 0, "nearest"),
                                       (15, 2, 2, "constant"), (5, 1, 0, "wrap"), (31, 5, 3, "interp")]:
        savgol.append({"window": window, "polyorder": order, "deriv": deriv, "mode": mode,
                       "y": floats(scipy.signal.savgol_filter(x, window, order, deriv=deriv, delta=1 / fs, mode=mode))})
    short = x[:7]
    savgol_short = [{"mode": mode, "x": floats(short), "y": floats(scipy.signal.savgol_filter(short, 11, 2, mode=mode))}
                    for mode in ["mirror", "nearest", "constant", "wrap"]]
    analytic = scipy.signal.hilbert(x)
    hilbert = {"imag": floats(analytic.imag), "phase": floats(np.angle(analytic)), "unwrapped": floats(np.unwrap(np.angle(analytic)))}
    remez = []
    for numtaps, bands, desired, weight, kind in [(63, [0, 800, 1200, 4000], [1, 0], None, "bandpass"),
                                                  (40, [0, 1000, 1500, 4000], [1, 0], [1, 10], "bandpass"),
                                                  (31, [200, 3800], [1], None, "hilbert"), (21, [0, 3200], [1], None, "differentiator")]:
        h = scipy.signal.remez(numtaps, bands, desired, weight=weight, type=kind, fs=fs)
        full = np.convolve(x, h)
        remez.append({"numtaps": numtaps, "bands": bands, "desired": desired, "weight": weight, "type": kind,
                      "y": floats(full[:n]), "centred": floats(full[(numtaps - 1) // 2:][:n])})
    firls = []
    for numtaps, bands, desired, weight in [(31, [0, 1000, 1500, 4000], [1, 1, 0, 0], None),
                                            (41, [0, 500, 800, 1600, 1900, 4000], [0, 0, 1, 1, 0, 0], [10, 1, 10])]:
        h = scipy.signal.firls(numtaps, bands, desired, weight=weight, fs=fs)
        full = np.convolve(x, h)
        firls.append({"numtaps": numtaps, "bands": bands, "desired": desired, "weight": weight,
                      "y": floats(full[:n]), "centred": floats(full[(numtaps - 1) // 2:][:n])})

    tone_n = 2400
    tt = np.arange(tone_n) / fs
    phase = 2 * np.pi * (200 * tt + 0.5 * (60 / tt[-1]) * tt ** 2)
    tone = np.sin(phase) + 0.4 * np.sin(2 * phase) + 0.2 * np.sin(3 * phase) + 0.05 * rng.standard_normal(tone_n)
    tone[1600:] = 0.0
    f0 = yin(tone, fs, 80.0, 600.0, 512, 128, 0.15)
    voiced = f0[f0 > 0]
    pitch = {"fmin": 80.0, "fmax": 600.0, "frame": 512, "hop": 128, "threshold": 0.15, "f0": floats(f0),
             "f0_median_hz": float(np.median(voiced)), "voiced_fraction": float(len(voiced) / len(f0)), "frames": float(len(f0))}
    mfcc = []
    for n_mfcc, n_mels, frame, hop in [(13, 20, 256, 128), (8, 40, 512, 256)]:
        energies = stft_power(tone, frame, hop) @ mel_bank(fs, frame, n_mels).T
        c = scipy.fft.dct(10 * np.log10(np.maximum(energies, 1e-10)), type=2, norm="ortho", axis=1)[:, :n_mfcc]
        mfcc.append({"n_mfcc": n_mfcc, "n_mels": n_mels, "frame": frame, "hop": hop, "mean": floats(c.mean(axis=0)), "frames": float(len(c))})
    features = [{"frame": frame, "hop": hop, "rolloff_pct": pct, "report": spectral_features(tone, fs, frame, hop, pct)}
                for frame, hop, pct in [(256, 64, 85.0), (512, 200, 50.0)]]

    hist = []
    for bins, lo_hi, data in [(10, None, x), (7, (-1.0, 1.0), x), (4, None, np.full(5, 0.25))]:
        counts, edges = np.histogram(data, bins=bins, range=lo_hi)
        k = int(np.argmax(counts))
        hist.append({"bins": bins, "range": "auto" if lo_hi is None else f"{lo_hi[0]} {lo_hi[1]}", "x": floats(data),
                     "counts": [int(v) for v in counts], "lo": float(edges[0]), "hi": float(edges[-1]),
                     "outside": float(len(data) - counts.sum()), "mode": float((edges[k] + edges[k + 1]) / 2)})

    noise = rng.standard_normal(320)
    base = scipy.signal.lfilter([0.25, 0.25, 0.25, 0.25], 1.0, noise)
    a = base[:300]
    later = 0.8 * np.concatenate([np.zeros(17), a[:-17]]) + 0.05 * rng.standard_normal(300)
    sooner = -base[5:305]
    spectrum = np.fft.rfft(a)
    fractional = np.fft.irfft(spectrum * np.exp(-2j * np.pi * np.fft.rfftfreq(300) * 3.4), 300)
    delay = [{"b": floats(b), "report": delay_report(a, b, fs)} for b in [later, sooner, fractional, a[:120]]]

    y = 0.6 * x + 0.4 * rng.standard_normal(n)
    correlation = []
    for p, q in [(x, y), (np.round(4 * x) / 4, np.round(4 * y) / 4), (x, np.concatenate([-x, [1.0, 2.0]]))]:
        m = min(len(p), len(q))
        pp, qq = p[:m], q[:m]
        correlation.append({"a": floats(p), "b": floats(q), "pearson": float(np.corrcoef(pp, qq)[0, 1]),
                            "cosine": float(np.dot(pp, qq) / np.sqrt(np.dot(pp, pp) * np.dot(qq, qq))),
                            "spearman": float(scipy.stats.spearmanr(pp, qq).statistic), "n": float(m)})

    bands = pywt.wavedec(x, "db2", mode="symmetric", level=3)
    names = ["a3", "d3", "d2", "d1"]
    peak = max(np.max(np.abs(c)) for c in bands)
    total = sum(np.dot(c, c) for c in bands)
    stats = {}
    for name, c in zip(names, bands):
        e = np.dot(c, c)
        share = c * c / e
        share = share[share > 0]
        stats[name] = {"energy": float(e / total), "entropy": float(-np.sum(share * np.log2(share))),
                       "sparsity": float(np.mean(np.abs(c) < 0.05 * peak))}
    return {"fs": fs, "x": floats(x), "lfilter": lfilter, "fir_taps": fir_taps, "savgol": savgol, "savgol_short": savgol_short,
            "hilbert": hilbert, "remez": remez, "firls": firls, "tone": floats(tone), "pitch": pitch, "mfcc": mfcc,
            "spectral_features": features, "histogram": hist, "delay_a": floats(a), "delay": delay, "correlation": correlation,
            "coeff_stats": {"wavelet": "db2", "levels": 3, "tol": 0.05, "bands": stats}}


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
                               ("processing.json", lambda: processing_json(sample_wav)),
                               ("iir.json", iir_json), ("fir.json", fir_json), ("ops.json", ops_json), ("analysis.json", analysis_json),
                               ("blocks.json", blocks_json), ("adaptive.json", adaptive_json), ("restoration.json", restoration_json), ("toeplitz.json", toeplitz_json), ("blocks_fx.json", blocks_fx_json), ("blocks_analysis.json", blocks_analysis_json), ("blocks2.json", blocks2_json)]:
        text = json.dumps(builder(), separators=(",", ":"), allow_nan=False)
        (args.out / file_name).write_text(text + "\n")
    args.tables.parent.mkdir(parents=True, exist_ok=True)
    args.tables.write_text(tables_inc())


if __name__ == "__main__":
    main()
