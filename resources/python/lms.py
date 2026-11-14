def lms(x, d, taps, mu, normalized):
    e, y = np.zeros_like(d), np.zeros_like(d)
    for ch in range(d.shape[0]):
        source = np.concatenate([np.zeros(taps - 1), x[min(ch, x.shape[0] - 1)]])
        w = np.zeros(taps)
        for n in range(d.shape[1]):
            u = source[n : n + taps][::-1]
            y[ch, n] = w @ u
            e[ch, n] = d[ch, n] - y[ch, n]
            w += (mu * e[ch, n] / (1e-8 + u @ u) if normalized else mu * e[ch, n]) * u
    return e, y
