# needs: correlate_full, energy
# import: import math
def delay_estimate(a, b, fs):
    report = {}
    for ch, x in enumerate(a):
        suffix = f"_ch{ch}" if a.shape[0] > 1 else ""
        y = b[min(ch, b.shape[0] - 1)]
        z = correlate_full(y, x)
        energies = energy(x) * energy(y)
        lag = peak_lag = peak = 0.0
        if energies > 0:
            k = int(np.argmax(np.abs(z)))
            delta = 0.0
            if 0 < k < z.size - 1:
                y0, y1, y2 = abs(z[k - 1]), abs(z[k]), abs(z[k + 1])
                if y0 - 2 * y1 + y2 < 0:
                    delta = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
            peak_lag = float(k - (x.size - 1))
            lag, peak = peak_lag + delta, z[k] / math.sqrt(energies)
        report["lag_samples" + suffix] = float(lag)
        report["lag_s" + suffix] = float(lag / fs)
        report["peak_lag" + suffix] = peak_lag
        report["peak_corr" + suffix] = float(peak)
    return report
