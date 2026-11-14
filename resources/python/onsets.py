# needs: mixdown
# import: import math
def onsets(x, fs, n, hop):
    mono = mixdown(x)[0]
    times = []
    if n and hop and mono.size >= n and fs > 0:
        window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n, dtype=np.float64) / n)
        previous = np.zeros(n // 2 + 1)
        flux = []
        for start in range(0, mono.size - n + 1, hop):
            m = np.abs(np.fft.rfft(mono[start : start + n] * window))
            f = 0.0
            for v in np.maximum(0.0, m - previous):
                f += v
            previous = m
            flux.append(f)
        peak = max(flux)
        if peak > 1e-12:
            half = max(1, int(math.floor(0.25 * fs / hop + 0.5)))
            gap = int(math.ceil(0.05 * fs / hop))
            last = None
            for m in range(len(flux)):
                rising = (m == 0 or flux[m] >= flux[m - 1]) and (m + 1 == len(flux) or flux[m] > flux[m + 1])
                if not rising:
                    continue
                around = np.array(flux[max(0, m - half) : min(len(flux), m + half + 1)])
                threshold = 1.5 * np.partition(around, around.size // 2)[around.size // 2] + 0.1 * peak
                if flux[m] <= threshold or (last is not None and m - last < gap):
                    continue
                times.append(max(0.0, m * hop + 0.75 * n - 0.5 * hop) / fs)
                last = m
    return {"count": float(len(times)), "times_s": " ".join(f"{t:.3f}" for t in times)}
