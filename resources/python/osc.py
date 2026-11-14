def osc(kind, f, duty, width, fs, n):
    k = np.arange(n, dtype=np.float64)
    if kind == "pulses":
        step = np.floor(k * f / fs)
        out = np.zeros(n)
        if n:
            out[0] = 1.0
            out[1:] = (step[1:] != np.floor((k[1:] - 1) * f / fs)) * 1.0
        return out
    t = k / fs
    tmod = np.fmod(2 * np.pi * f * t, 2 * np.pi)
    if kind == "square":
        return np.where(tmod < duty * 2 * np.pi, 1.0, -1.0)
    if kind == "triangle":
        width = 0.5
    with np.errstate(divide="ignore", invalid="ignore"):
        rising = tmod / (np.pi * width) - 1
        falling = (np.pi * (width + 1) - tmod) / (np.pi * (1 - width))
    return np.where(tmod < width * 2 * np.pi, rising, falling)
