# import: import math
def thd(x, fs, harmonics):
    out = {}
    lobe = 4
    for c, row in enumerate(x):
        n = row.size
        t = 2 * np.pi * np.arange(n, dtype=np.float64) / n
        spectrum = np.fft.rfft(row * (0.35875 - 0.48829 * np.cos(t) + 0.14128 * np.cos(2 * t) - 0.01168 * np.cos(3 * t)))
        power = spectrum.real ** 2 + spectrum.imag ** 2
        top = power.size - 1
        peak = int(np.argmax(power[1:-1])) + 1
        a, b, cc = math.log(power[peak - 1] + 1e-300), math.log(power[peak]), math.log(power[peak + 1] + 1e-300)
        denom = a - 2 * b + cc
        position = peak + (min(max(0.5 * (a - cc) / denom, -0.5), 0.5) if denom != 0 else 0.0)

        def band(at):
            centre = int(math.floor(at + 0.5))
            total = 0.0
            for j in range(max(0, centre - lobe), min(top, centre + lobe) + 1):
                total += power[j]
            return total

        fundamental = band(position)
        rest = 0.0
        for h in range(2, harmonics + 1):
            at = position * h
            if at + lobe > top:
                break
            rest += band(at)
        ratio = math.sqrt(rest / fundamental)
        suffix = f"_ch{c}" if x.shape[0] > 1 else ""
        out["f0_hz" + suffix] = position * fs / n
        out["thd_percent" + suffix] = 100 * ratio
        out["thd_db" + suffix] = 20 * math.log10(max(ratio, 1e-15))
    return out
