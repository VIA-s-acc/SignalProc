"""Data for the canvas screens «Спектр» and «Обработка» (test.wav, real numbers)."""
import json
import sys
import wave

import numpy as np
import pywt
from scipy import signal

wav, out_dir = sys.argv[1], sys.argv[2]
w = wave.open(wav, 'rb')
fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(float) / 32768
left = x[:, 0]
dur = len(left) / fs

COLS, FBINS, FMIN, FMAX, FLOOR = 100, 36, 30.0, fs / 2, -90.0
fedges = np.geomspace(FMIN, FMAX, FBINS + 1)
NOTES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B']


def note(f):
    m = 69 + 12 * np.log2(f / 440)
    k = int(round(m))
    return f'{NOTES[k % 12]}{k // 12 - 1}', int(round(100 * (m - k)))


def spectrogram(n):
    f, t, z = signal.stft(left, fs, window='hann', nperseg=n, noverlap=3 * n // 4, boundary=None, padded=False)
    p = np.abs(z) ** 2
    tcol = np.minimum((t / dur * COLS).astype(int), COLS - 1)
    cols = np.stack([p[:, tcol == c].mean(1) for c in range(COLS)], axis=1)
    rows = []
    for a, b in zip(fedges[:-1], fedges[1:]):
        m = (f >= a) & (f < b)
        rows.append(cols[m].mean(0) if m.any() else cols[np.argmin(abs(f - np.sqrt(a * b)))])
    db = 10 * np.log10(np.array(rows) / np.max(rows) + 1e-30)
    return [[int(round(max(v, FLOOR))) for v in row] for row in db[::-1]]


sel = left[int(2.10 * fs):int(4.85 * fs)]
fw, pw = signal.welch(sel, fs, window='hann', nperseg=16384, noverlap=8192)
pw_db = 10 * np.log10(pw / pw.max() + 1e-30)


def resample_curve(fgrid):
    out = []
    for a, b in zip(fgrid[:-1], fgrid[1:]):
        m = (fw >= a) & (fw < b)
        v = pw_db[m].max() if m.any() else pw_db[np.argmin(abs(fw - (a + b) / 2))]
        out.append(int(round(max(v, -100))))
    return out


peaks_idx, _ = signal.find_peaks(pw_db, prominence=12, distance=8)
peaks_idx = [i for i in peaks_idx if 40 <= fw[i] <= 8000]
peaks_idx = sorted(peaks_idx, key=lambda i: -pw_db[i])[:6]
peaks = []
for i in sorted(peaks_idx, key=lambda i: fw[i]):
    nm, cents = note(fw[i])
    peaks.append(dict(f=round(float(fw[i]), 1), db=round(float(pw_db[i]), 1), note=nm, cents=cents))
f0 = min((p for p in peaks if p['f'] < 600), key=lambda p: -p['db'])['f']
harm = []
for k in range(1, 9):
    m = (fw > k * f0 * 0.98) & (fw < k * f0 * 1.02)
    j = np.argmax(np.where(m, pw_db, -1e9))
    harm.append(dict(k=k, f=round(float(fw[j]), 1), db=round(float(pw_db[j]), 1)))

spectrum = dict(
    meta=dict(fs=fs, dur=round(dur, 3), cols=COLS, fbins=FBINS, fmin=FMIN, fmax=FMAX, floor=FLOOR, sel=[2.10, 4.85]),
    wide=dict(n=512, grid=spectrogram(512)),
    narrow=dict(n=4096, grid=spectrogram(4096)),
    spec_log=resample_curve(np.geomspace(20, fs / 2, 201)),
    spec_lin=resample_curve(np.linspace(0, fs / 2, 201)),
    peaks=peaks, f0=f0, harmonics=harm,
)
json.dump(spectrum, open(f'{out_dir}/spectrum.json', 'w'), separators=(',', ':'), ensure_ascii=False)

N, J, WAVE = 8192, 5, 'sym8'
clean = left[100_000:100_000 + N]
sigma_noise = np.sqrt(np.mean(clean ** 2) / 10)
noisy = clean + np.random.default_rng(7).normal(0, sigma_noise, N)
coeffs = pywt.wavedec(noisy, WAVE, level=J, mode='symmetric')
d1 = coeffs[-1]
sigma = np.median(np.abs(d1[d1 != 0])) / 0.6744897501960817
bayes = [float(sigma ** 2 / np.sqrt(max(np.mean(c ** 2) - sigma ** 2, np.finfo(float).eps))) for c in coeffs[1:]]
visu = float(sigma * np.sqrt(2 * np.log(N)))
names = [f'a{J}'] + [f'd{j}' for j in range(J, 0, -1)]
total = sum(c.size for c in coeffs)
peak = np.abs(noisy).max()
DOTS = 200


def env(sig, scale, cols=DOTS):
    e = np.linspace(0, len(sig), cols + 1).astype(int)
    return [[int(round(sig[a:b].min() / scale * 1000)), int(round(sig[a:b].max() / scale * 1000))] for a, b in zip(e[:-1], e[1:])]


def snr(ref, est):
    return 10 * np.log10(np.sum(ref ** 2) / np.sum((ref - est) ** 2))


bands = []
for k, (nm, c) in enumerate(zip(names, coeffs)):
    mx = float(np.abs(c).max())
    j = J if k == 0 else J + 1 - k
    lo, hi = (0.0, fs / 2 ** (J + 1)) if k == 0 else (fs / 2 ** (j + 1), fs / 2 ** j)
    bands.append(dict(name=nm, len=int(c.size), max=mx, lo=lo, hi=hi, env=env(c, mx, 160),
                      bayes=None if k == 0 else bayes[k - 1]))

SCALES = [0.5, 0.75, 1.0, 1.5, 2.0]
combos = {}
for rule in ['bayes', 'visu']:
    for mode in ['soft', 'hard']:
        for s in SCALES:
            new = [coeffs[0]]
            kept = coeffs[0].size
            kept_bands = []
            for k, c in enumerate(coeffs[1:]):
                lam = s * (bayes[k] if rule == 'bayes' else visu)
                new.append(pywt.threshold(c, lam, mode))
                kept_bands.append(int(np.count_nonzero(np.abs(c) >= lam)))
                kept += kept_bands[-1]
            y = pywt.waverec(new, WAVE, mode='symmetric')[:N]
            combos[f'{rule}/{mode}/{s}'] = dict(
                out=env(y, peak), res=env(noisy - y, peak), snr_out=round(snr(clean, y), 2),
                kept=kept, kept_bands=kept_bands, mse=float(np.mean((y - clean) ** 2)), energy=round(100 * np.sum(y ** 2) / np.sum(noisy ** 2), 1),
                err_db=round(10 * np.log10(np.sum((noisy - y) ** 2) / np.sum(noisy ** 2)), 2))

flat = np.concatenate(coeffs)
order = np.argsort(-np.abs(flat))
curve = []
for K in np.unique(np.geomspace(1, total, 60).astype(int)):
    keep = np.zeros_like(flat)
    keep[order[:K]] = flat[order[:K]]
    parts, at = [], 0
    for c in coeffs:
        parts.append(keep[at:at + c.size]); at += c.size
    y = pywt.waverec(parts, WAVE, mode='symmetric')[:N]
    curve.append([int(K), round(10 * np.log10(np.sum((noisy - y) ** 2) / np.sum(noisy ** 2)), 2)])

NP, P = 1024, 2 ** J
bins = np.unique(np.geomspace(4, NP // 2 - 1, 120).astype(int))


def transfer(gains):
    t_db, a_db = [], []
    for m in bins:
        n = np.arange(NP)
        ys = []
        for part in (np.cos(2 * np.pi * m * n / NP), np.sin(2 * np.pi * m * n / NP)):
            c = pywt.wavedec(part, WAVE, level=J, mode='periodization')
            ys.append(pywt.waverec([g * b for g, b in zip(gains, c)], WAVE, mode='periodization'))
        Y = np.fft.fft(ys[0] + 1j * ys[1]) / NP
        others = [(m + k * NP // P) % NP for k in range(1, P)]
        t_db.append(round(20 * np.log10(abs(Y[m]) + 1e-12), 2))
        a_db.append(round(10 * np.log10(np.sum(np.abs(Y[others]) ** 2) + 1e-24), 2))
    return t_db, a_db


presets = {}
for key, gains in [('flat', [1, 1, 1, 1, 1, 1]), ('bass', [2, 2, 1.4, 1, 1, 1]),
                   ('no_d1', [1, 1, 1, 1, 1, 0]), ('notch_d3', [1, 1, 1, 0, 1, 1])]:
    t_db, a_db = transfer(gains)
    presets[key] = dict(gains=gains, t=t_db, alias=a_db)

processing = dict(
    meta=dict(fs=fs, n=N, start=100_000, wavelet=WAVE, levels=J, snr_in=round(snr(clean, noisy), 2), sigma=float(sigma),
              visu=visu, peak=float(peak), total=int(total), scales=SCALES),
    noisy=env(noisy, peak), clean=env(clean, peak), bands=bands, combos=combos, curve=curve,
    eq=dict(omega=[round(float(2 * m / NP), 5) for m in bins], presets=presets),
)
json.dump(processing, open(f'{out_dir}/processing.json', 'w'), separators=(',', ':'), ensure_ascii=False)
print('spectrum peaks', peaks, 'f0', f0)
print('harmonics', harm)
print('snr_in', processing['meta']['snr_in'], 'sigma', sigma, 'bayes', [round(v, 4) for v in bayes], 'visu', round(visu, 4))
for k in ['bayes/soft/1.0', 'visu/soft/1.0', 'bayes/hard/1.0']:
    c = combos[k]
    print(k, c['snr_out'], c['kept'], total, c['energy'], c['err_db'])
for k, p in presets.items():
    print(k, 'T range', min(p['t']), max(p['t']), 'alias max', max(p['alias']))
