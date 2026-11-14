def fade(x, rise, fall, cosine):
    out = np.array(x, dtype=np.float64)
    n = out.shape[1]

    def gain(i, length):
        t = i / length
        return 0.5 - 0.5 * np.cos(np.pi * t) if cosine else t

    i = np.arange(min(rise, n), dtype=np.float64)
    out[:, : i.size] *= gain(i, rise)
    i = np.arange(min(fall, n), dtype=np.float64)
    if i.size:
        out[:, n - 1 - np.arange(i.size)] *= gain(i, fall)
    return out
