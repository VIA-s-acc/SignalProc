def wiener(x, size, noise):
    out = np.empty_like(x)
    for ch, row in enumerate(x):
        padded = np.pad(row, size // 2)
        s, s2 = np.zeros(row.size), np.zeros(row.size)
        for k in range(size):
            s += padded[k : k + row.size]
            s2 += padded[k : k + row.size] * padded[k : k + row.size]
        mean = s / size
        var = s2 / size - mean * mean
        level = noise if noise > 0 else np.cumsum(var)[-1] / row.size
        with np.errstate(all="ignore"):
            out[ch] = np.where((var < level) | ~(var > 0), mean, (row - mean) * (1 - level / var) + mean)
    return out
