# import: from scipy import signal
def inst_freq(x, fs):
    rows = []
    for row in x:
        a = signal.hilbert(row)
        if a.size < 2:
            rows.append(np.zeros(0))
            continue
        phase = np.unwrap(np.angle(a))
        mag = np.abs(a)
        peak = np.max(mag)
        out = np.zeros(a.size - 1)
        for n in range(a.size - 1):
            quiet = mag[n] <= 1e-12 * peak or mag[n + 1] <= 1e-12 * peak
            out[n] = (out[n - 1] if n else 0.0) if quiet else (phase[n + 1] - phase[n]) * fs / (2 * np.pi)
        rows.append(out)
    return np.stack(rows)
