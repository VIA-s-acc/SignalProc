# import: import math
def _multirate_filter(m, attenuation_db, transition=0.1):
    a = abs(attenuation_db)
    width = transition / m
    taps = int(math.ceil((a - 7.95) / 2.285 / (np.pi * width) + 1)) | 1
    beta = 0.1102 * (a - 8.7) if a > 50 else (0.5842 * (a - 21) ** 0.4 + 0.07886 * (a - 21) if a > 21 else 0.0)
    if taps == 1:
        return np.ones(1)
    alpha = 0.5 * (taps - 1)
    k = np.arange(taps, dtype=np.float64) - alpha
    r = k / alpha
    cutoff = 1.0 / m
    h = cutoff * np.sinc(cutoff * k) * (np.i0(beta * np.sqrt(1 - r * r)) / np.i0(beta))
    return h / math.fsum(h)


def multirate(x, up, down, attenuation_db, anti_alias):
    rows = []
    for row in x:
        y = row
        if up > 1:
            stuffed = np.zeros(y.size * up)
            stuffed[::up] = y
            if anti_alias and y.size:
                h = _multirate_filter(up, attenuation_db) * up
                d = (h.size - 1) // 2
                y = np.convolve(stuffed, h)[d : d + stuffed.size]
            else:
                y = stuffed
        if down > 1:
            n = (y.size + down - 1) // down
            if anti_alias:
                h = _multirate_filter(down, attenuation_db)
                d = (h.size - 1) // 2
                y = np.convolve(y, h)[d : d + n * down : down][:n]
            else:
                y = y[::down][:n]
        rows.append(y)
    return np.stack(rows)
