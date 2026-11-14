def window_of(kind, n, beta, alpha):
    if n < 2:
        return np.ones(n)
    last = n - 1
    k = np.arange(n, dtype=np.float64)
    if kind == "tukey":
        if alpha <= 0:
            return np.ones(n)
        if alpha < 1:
            w = np.ones(n)
            width = int(np.floor(alpha * last / 2.0))
            head = np.arange(0, width + 1, dtype=np.float64)
            w[: width + 1] = 0.5 * (1 + np.cos(np.pi * (-1 + 2.0 * head / alpha / last)))
            tail = np.arange(n - width - 1, n, dtype=np.float64)
            w[n - width - 1 :] = 0.5 * (1 + np.cos(np.pi * (-2.0 / alpha + 1 + 2.0 * tail / alpha / last)))
            return w
        kind = "hann"
    t = 2 * np.pi * k / last
    if kind == "hann":
        return 0.5 - 0.5 * np.cos(t)
    if kind == "hamming":
        return 0.54 - 0.46 * np.cos(t)
    if kind == "blackman":
        return 0.42 - 0.5 * np.cos(t) + 0.08 * np.cos(2 * t)
    r = 2 * k / last - 1
    return np.i0(beta * np.sqrt(np.maximum(0.0, 1 - r * r))) / np.i0(beta)
