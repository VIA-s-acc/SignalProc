# needs: stft_power
# import: import math
# import: from scipy import fft as sp_fft
def mfcc(x, fs, count, mels, n, hop):
    top = 2595 * math.log10(1 + fs / 2 / 700)
    edges = [700 * (10.0 ** ((top if k == mels + 1 else k * (top / (mels + 1))) / 2595) - 1) for k in range(mels + 2)]
    f = np.arange(n // 2 + 1) * fs / n
    bank = np.array([np.maximum(0.0, np.minimum((f - edges[m]) / (edges[m + 1] - edges[m]), (edges[m + 2] - f) / (edges[m + 2] - edges[m + 1]))) for m in range(mels)])
    report = {}
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        frames = stft_power(row, n, hop) if row.size >= n else []
        sums = np.zeros(count)
        for power in frames:
            energies = 10 * np.log10(np.maximum(np.cumsum(bank * power, axis=1)[:, -1], 1e-10))
            sums += sp_fft.dct(energies, type=2, norm="ortho")[:count]
        report["frames" + suffix] = float(len(frames))
        report.update({f"mfcc{k}{suffix}": float(sums[k] / len(frames)) if frames else 0.0 for k in range(count)})
    return report
