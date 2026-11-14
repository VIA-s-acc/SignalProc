# needs: stft_power
# import: import math
def _spectral_mean(v):
    return np.cumsum(v)[-1] / len(v) if v else 0.0


def spectral_features(x, fs, n, hop, pct):
    report = {}
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        centroid, spread, rolloff, flatness, flux = [], [], [], [], []
        frames = stft_power(row, n, hop) if row.size >= n else []
        previous = None
        for power in frames:
            m = np.sqrt(power)
            cumulative = np.cumsum(m)
            total = cumulative[-1]
            if previous is not None:
                flux.append(math.sqrt(np.cumsum((m - previous) * (m - previous))[-1]))
            previous = m
            if not total > 0:
                continue
            f = np.arange(m.size) * fs / n
            c = np.cumsum(f * m)[-1] / total
            reached = np.nonzero(cumulative >= pct / 100 * total)[0]
            floored = np.maximum(power, 1e-10)
            centroid.append(c)
            spread.append(math.sqrt(np.cumsum((f - c) * (f - c) * m)[-1] / total))
            rolloff.append(f[reached[0]] if reached.size else f[-1])
            flatness.append(math.exp(np.cumsum(np.log(floored))[-1] / m.size) / (np.cumsum(floored)[-1] / m.size))
        for name, values in (("centroid_hz", centroid), ("spread_hz", spread), ("rolloff_hz", rolloff), ("flatness", flatness), ("flux", flux)):
            report[name + suffix] = float(_spectral_mean(values))
        report["frames" + suffix] = float(len(frames))
    return report
