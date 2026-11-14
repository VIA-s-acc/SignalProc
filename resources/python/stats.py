# import: import math
# import: from scipy import signal
def _stats_spectrum(row, fs):
    segment = min(row.size, 4096)
    if segment < 2:
        return 0.0, 0.0
    f, p = signal.welch(row, fs, window="hann", nperseg=segment, noverlap=segment // 2, detrend="constant", scaling="density")
    total = moment = 0.0
    for fk, pk in zip(f, p):
        total += pk
        moment += fk * pk
    if not total > 0:
        return 0.0, 0.0
    k = int(np.argmax(p))
    offset = 0.0
    if 0 < k < p.size - 1 and p[k - 1] > 0 and p[k + 1] > 0:
        a, b, c = math.log(p[k - 1]), math.log(p[k]), math.log(p[k + 1])
        curve = a - 2 * b + c
        if curve < 0:
            offset = 0.5 * (a - c) / curve
    return moment / total, (k + offset) * fs / segment


def stats(x, fs):
    out = {}
    for ch, row in enumerate(x):
        suffix = f"_ch{ch}" if x.shape[0] > 1 else ""
        n = row.size
        total = squares = peak = 0.0
        crossings = 0
        for i in range(n):
            total += row[i]
            squares += row[i] * row[i]
            peak = max(peak, abs(row[i]))
            if i and (row[i - 1] >= 0) != (row[i] >= 0):
                crossings += 1
        mean = total / n if n else 0.0
        rms = math.sqrt(squares / n) if n else 0.0
        spread = 0.0
        for v in row:
            spread += (v - mean) * (v - mean)
        centroid, dominant = _stats_spectrum(row, fs)
        out["rms" + suffix] = rms
        out["peak" + suffix] = peak
        out["crest_db" + suffix] = 20 * math.log10(peak / rms) if rms > 0 else 0.0
        out["mean" + suffix] = mean
        out["std" + suffix] = math.sqrt(spread / n) if n else 0.0
        out["zcr_per_s" + suffix] = crossings * fs / n if n else 0.0
        out["centroid_hz" + suffix] = centroid
        out["dominant_hz" + suffix] = dominant
    return out
