# needs: correlate_full
# import: import math
def _stretch_window(n):
    return 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)


def _stretch_lock(acc, mag, analysis):
    peak = np.ones(mag.size, dtype=bool)
    for d in (1, 2):
        peak[d:] &= mag[d:] > mag[:-d]
        peak[:-d] &= mag[:-d] > mag[d:]
    peaks = np.nonzero(peak)[0]
    if not peaks.size:
        return acc
    k = np.arange(mag.size)
    p = peaks[np.searchsorted(peaks[:-1] + peaks[1:], 2 * k, side="left")]
    return np.where(k != p, acc[p] + (analysis - analysis[p]), acc)


def _stretch_vocoder(x, rate, n, hop, locked):
    window, half, bins = _stretch_window(n), n // 2, n // 2 + 1
    padded = np.concatenate([np.zeros(half), x, np.zeros(n)])
    spectra = [np.fft.rfft(padded[k * hop : k * hop + n] * window) for k in range(1 + x.size // hop)]
    mags, phases = [np.hypot(s.real, s.imag) for s in spectra], [np.angle(s) for s in spectra]
    n_in, length = len(spectra), round(x.size / rate)
    y, norm = np.zeros(length), np.zeros(length)
    acc, diff = np.zeros(bins), np.zeros(bins)
    for j in range(math.ceil(n_in / rate)):
        t = j * rate
        i0 = math.floor(t)
        i1 = min(i0 + 1, n_in - 1)
        if n_in == 1:
            mag = mags[0]
        else:
            lo = min(max(math.ceil(t), 1), n_in - 1) - 1
            mag = (mags[lo + 1] - mags[lo]) * (t - lo) + mags[lo]
        if j == 0:
            acc = phases[i0].copy()
        else:
            acc = acc + diff
            if locked:
                acc = _stretch_lock(acc, mag, phases[i0] if t - i0 < 0.5 else phases[i1])
        diff = phases[i1] - phases[i0]
        spectrum = np.empty(bins, dtype=complex)
        spectrum.real, spectrum.imag = mag * np.cos(acc), mag * np.sin(acc)
        frame = np.fft.irfft(spectrum, n)
        first, last = max(0, half - j * hop), min(n, length + half - j * hop)
        if first < last:
            y[j * hop + first - half : j * hop + last - half] += window[first:last] * frame[first:last]
            norm[j * hop + first - half : j * hop + last - half] += window[first:last] * window[first:last]
    np.divide(y, norm, out=y, where=norm > np.finfo(float).tiny)
    return y


def _stretch_round(v):
    return math.floor(v) + (v - math.floor(v) >= 0.5)


def _stretch_wsola(x, rate, n, hop):
    tol, half, length = max(hop, n // 4), n // 2, round(x.size / rate)
    window = _stretch_window(n)

    def segment(start, count):
        out = np.zeros(count)
        lo, hi = max(start, 0), min(start + count, x.size)
        if lo < hi:
            out[lo - start : hi - start] = x[lo:hi]
        return out

    order = np.array([0] + [s for d in range(1, tol + 1) for s in (-d, d)])
    y, norm = np.zeros(length), np.zeros(length)
    previous = 0
    for m in range((length + n // 2 + hop - 1) // hop):
        ideal = _stretch_round(float(m * hop) * rate) - half
        start = ideal
        if m > 0:
            region = segment(ideal - tol, n + 2 * tol)
            cross = correlate_full(region, segment(previous + hop, n))[n - 1 : n + 2 * tol]
            energy = np.concatenate([[0.0], np.cumsum(region * region)])
            at = order + tol
            e = energy[at + n] - energy[at]
            with np.errstate(all="ignore"):
                score = np.where(e > 1e-300, cross[at] / np.sqrt(e), 0.0)
            start = ideal + int(order[np.argmax(score)])
        first, last = max(0, half - m * hop), min(n, length + half - m * hop)
        if first < last:
            y[m * hop + first - half : m * hop + last - half] += window[first:last] * segment(start, n)[first:last]
            norm[m * hop + first - half : m * hop + last - half] += window[first:last]
        previous = start
    np.divide(y, norm, out=y, where=norm > 1e-12)
    return y


def time_stretch(x, rate, method, n, hop):
    if x.shape[1] == 0:
        return x.copy()
    stretch = (lambda row: _stretch_wsola(row, rate, n, hop)) if method == "wsola" else (lambda row: _stretch_vocoder(row, rate, n, hop, method == "locked"))
    return np.stack([stretch(row) for row in x])
